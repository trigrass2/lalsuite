#include <lal/LALInspiral.h>
#include <lal/LALStdlib.h>
#include <lal/GenerateInspiral.h>
#include <lal/GeneratePPNInspiral.h>
#include <lal/SeqFactories.h>
#include <lal/Units.h>



NRCSID( GENERATEINSPIRALC, "$Id$" );

#define GENERATEINSPIRALC_ENORM 0
#define GENERATEINSPIRALC_ESUB  1
#define GENERATEINSPIRALC_EARG  2
#define GENERATEINSPIRALC_EVAL  3
#define GENERATEINSPIRALC_EFILE 4
#define GENERATEINSPIRALC_EMEM  5

#define GENERATEINSPIRALC_MSGENORM "Normal exit"
#define GENERATEINSPIRALC_MSGESUB  "Subroutine failed"
#define GENERATEINSPIRALC_MSGEARG  "Error parsing arguments"
#define GENERATEINSPIRALC_MSGEVAL  "Input argument out of valid range"
#define GENERATEINSPIRALC_MSGEFILE "Could not open file"
#define GENERATEINSPIRALC_MSGEMEM  "Out of memory"


#define SUB( func, statusptr )                                       \
do                                                                   \
if ( (func), (statusptr)->statusCode )                               \
{                                                                    \
  ERROR( GENERATEINSPIRALC_ESUB, GENERATEINSPIRALC_MSGESUB,                      \
         "Function call \"" #func "\" failed:" );                    \
  exit( GENERATEINSPIRALC_ESUB );                                          \
}                                                                    \
while (0)
#define ERROR( code, msg, statement )                                \
do                                                                   \
if ( lalDebugLevel & LALERROR )                                      \
{                                                                    \
  LALPrintError( "Error[0] %d: program %s, file %s, line %d, %s\n"   \
		 "        %s %s\n", (code), program, __FILE__,       \
		 __LINE__, GENERATEINSPIRALC, statement ? statement :      \
                 "", (msg) );                                        \
}                                                                    \
while (0)
char *program;

void
LALGenerateInspiral(
		    LALStatus             *status,
		    CoherentGW            *waveform,
		    GeneralInspiralStruc  *params
		    )
{

  /* System-derived constants. */
  REAL4 mTot, mu;      /* total mass and reduced mass */
  REAL4 eta, etaInv;   /* mass ratio and its inverse */
  REAL4 phiC;          /* phase at coalescence */
  REAL4 cosI;          /* cosine of system inclination */
  REAL4 fFac;          /* SI normalization for f and t */
  REAL4 f2aFac;        /* factor multiplying f in amplitude function */
  REAL4 yOld,  apFac, acFac;  /* extra factor in plus and cross amplitudes */

  /* Integration parameters. */

  UINT4  nMax;   /* index over timesteps, and its maximum + 1 */
  CreateVectorSequenceIn in;


  static REAL4Vector 
    *inject_hp, 
    *inject_hc, 
    *inject_freq, 
    *inject_phase;
  
  REAL8 dt;
  UINT4 n;

  /*  LALAssert(approximant);*/

  INITSTATUS(status, "LALGenerateInspiral",GENERATEINSPIRALC);
  ATTATCHSTATUSPTR(status);
  

  dt = 1./params->inspiral.tSampling;
  




    
  if (params->method == 0){
    LALInspiralWaveLength(status->statusPtr, &n, params->inspiral);
    CHECKSTATUSPTR(status);      
    printf(" #     here length = %d et approx=%d\n", n, params->inspiral.approximant);
    
    
    LALInspiralParameterCalc(status->statusPtr, &params->inspiral);
    CHECKSTATUSPTR(status);      

    
    
    LALCreateVector(status->statusPtr, &inject_hp, n);
    LALCreateVector(status->statusPtr, &inject_hc, n);
    LALCreateVector(status->statusPtr, &inject_freq, n);
    LALCreateVector(status->statusPtr, &inject_phase, n);
    CHECKSTATUSPTR(status);      

  


    switch(params->inspiral.approximant)
      {
      case TaylorT1: 
	LALInspiralWave1ForInjection(status->statusPtr, 
				     inject_hc, 
				     inject_hp,
				     inject_phase, 
				     inject_freq,
				     &params->inspiral);
	break;
      case TaylorT2: 
	LALInspiralWave2ForInjection(status->statusPtr, 
				     inject_hc, 
				     inject_hp,
				     inject_phase, 
				     inject_freq,
				     &params->inspiral);
	break;
      case TaylorT3: 
	LALInspiralWave3ForInjection(status->statusPtr, 
				     inject_hc, 
				     inject_hp,
				     inject_phase, 
				     inject_freq,
				     &params->inspiral);
	break;
      case TaylorF1:
      case TaylorF2:      
      case PadeT1:
      case PadeF1:
      case BCV:
      case BCVSpin:
      case SpinTaylorT3:
      case EOB:       
	LALEOBWaveformForInjection(status->statusPtr, 
				   inject_hc, 
				   inject_hp,
				   inject_phase, 
				   inject_freq,
				   &params->inspiral);
	break;
      default: 
	fprintf(stderr,"nothing to do (bad approximant?)");
	break;
      }



   nMax = 0;
   UINT4 padding=0;    
   // on avance pour eviter le padding avant le signal
   while (inject_freq->data[padding]==0) 
     padding++;
   //recherche dichotomique du max peut etre
   while (inject_freq->data[padding + nMax]!=0  &&  (padding+nMax) < inject_freq->length)
     {
       nMax++;
     }


  /*******************************************************************
   * COMPUTE SYSTEM PARAMETERS                                       *
   *******************************************************************/


   etaInv = 2.0 / eta;
   mTot = params->inspiral.mass1 +params->inspiral.mass2;
   eta = params->inspiral.mass1 *params->inspiral.mass2;
   eta/=mTot;
   eta/=mTot;

   mu = eta * mTot;
   cosI = cos( params->inspiral.inclination );//
      
   /* Compute frequency, phase, and amplitude factors. */
   fFac = 1.0 / ( 4.0*LAL_TWOPI*LAL_MTSUN_SI*mTot );
   dt = -1. * eta / ( params->inspiral.tSampling * 5.0*LAL_MTSUN_SI*mTot );      
   f2aFac = LAL_PI*LAL_MTSUN_SI*mTot*fFac;   
   apFac = acFac = -2.0*mu*LAL_MRSUN_SI/params->inspiral.distance;
   apFac *= 1.0 + cosI*cosI;
   acFac *= 2.0*cosI;
   
   

  /*******************************************************************
   * GENERATE WAVEFORM                                               *
   *******************************************************************/

  if ( ( waveform->a = (REAL4TimeVectorSeries *)
	 LALMalloc( sizeof(REAL4TimeVectorSeries) ) ) == NULL ) {
    ABORT( status->statusPtr, GENERATEPPNINSPIRALH_EMEM,
	   GENERATEPPNINSPIRALH_MSGEMEM );
  }
  memset( waveform->a, 0, sizeof(REAL4TimeVectorSeries) );
  if ( ( waveform->f = (REAL4TimeSeries *)
	 LALMalloc( sizeof(REAL4TimeSeries) ) ) == NULL ) {

    LALFree( waveform->a ); waveform->a = NULL;
    ABORT( status->statusPtr, GENERATEPPNINSPIRALH_EMEM,
	   GENERATEPPNINSPIRALH_MSGEMEM );
  }
  memset( waveform->f, 0, sizeof(REAL4TimeSeries) );
  if ( ( waveform->phi = (REAL8TimeSeries *)
	 LALMalloc( sizeof(REAL8TimeSeries) ) ) == NULL ) {
    LALFree( waveform->a ); waveform->a = NULL;
    LALFree( waveform->f ); waveform->f = NULL;
    ABORT( status->statusPtr, GENERATEPPNINSPIRALH_EMEM,
	   GENERATEPPNINSPIRALH_MSGEMEM );
  }
  CHECKSTATUSPTR(status);
  memset( waveform->phi, 0, sizeof(REAL8TimeSeries) );
  CHECKSTATUSPTR(status);


  in.length = nMax;
  in.vectorLength = 2;
  LALSCreateVectorSequence( status->statusPtr, &( waveform->a->data ), &in );
  LALSCreateVector( status->statusPtr, &( waveform->f->data ), nMax );
  LALDCreateVector( status->statusPtr, &( waveform->phi->data ), nMax );
  


  n = 0;
  phiC = inject_phase->data[nMax+ padding -1];
  
  
  while ( n < nMax ) {
    waveform->f->data->data[n]     =  inject_freq->data[n+padding];    
    waveform->a->data->data[2*n]   =  apFac * inject_hc->data[n+padding];
    waveform->a->data->data[2*n+1] =  acFac * inject_hp->data[n+padding];
    waveform->phi->data->data[n]   =  inject_phase->data[n +padding] - phiC;    
    n++;           
  }
  
  
  yOld = params->inspiral.fFinal  ;
  params->ppn.tc = n/params->inspiral.tSampling * ( 5.0*LAL_MTSUN_SI*mTot ) / eta;

  /*******************************************************************
   * CLEANUP                                                         *
   *******************************************************************/

  waveform->a->epoch = waveform->f->epoch = waveform->phi->epoch
    = params->ppn.epoch;
  waveform->a->deltaT = waveform->f->deltaT = waveform->phi->deltaT
    = params->ppn.deltaT;
  waveform->a->sampleUnits = lalStrainUnit;
  waveform->f->sampleUnits = lalHertzUnit;
  waveform->phi->sampleUnits = lalDimensionlessUnit;


  //  params->ppn->dfdt = dyMax*fFac*params->deltaT;
   params->ppn.dfdt = 1.*fFac*params->ppn.deltaT;
  params->ppn.fStop = yOld*fFac;
  params->ppn.length = n;


  SUB( LALDestroyVector(status->statusPtr, &inject_hp),status->statusPtr);

  LALDestroyVector(status->statusPtr, &inject_hc);

  LALDestroyVector(status->statusPtr, &inject_freq);

  LALDestroyVector(status->statusPtr, &inject_phase);  


  CHECKSTATUSPTR(status);



  }
  else
    {
      switch(params->method)
	{
	case PPN:
	  printf("# here ppn\n");
	  SUB(
	      LALGeneratePPNInspiral(status->statusPtr, waveform, &params->ppn),
	      status->statusPtr);
	  printf("# here ppn gfin \n");

	  break;
	case SpinOrbitCW:

	  SUB(
	      LALGenerateSpinOrbitCW(status->statusPtr, waveform, &params->socw),
	      status->statusPtr);
	  break;
	case TaylorCW:
	  SUB(
	      LALGenerateTaylorCW(status->statusPtr, waveform, &params->taylorcw),
	      status->statusPtr);
	  break;
	}
    }
  
  
  /*if from inspiral pacakge additional computation are needed*/
  
  
  
  
  
  
  


  DETATCHSTATUSPTR( status );
}

