/*
 * Copyright (C) 2007 Badri Krishnan, Chad Hanna, Lucia Santamaria Lara, Robert Adam Mercer, Stephen Fairhurst
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with with program; see the file COPYING. If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA  02111-1307  USA
 *
 * Revision: $Id$
 */


/**
 * \file ninja.c
 * \author Badri Krishnan
 * \brief Code for parsing and selecting numerical relativity 
 *        waves in frame files
 */


#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
 
#include <lalapps.h>
#include <FrameL.h>

#include <lal/LALConfig.h>
#include <lal/LALStdio.h>
#include <lal/LALStdlib.h>
#include <lal/LALError.h>
#include <lal/LALDatatypes.h>
#include <lal/LIGOMetadataTables.h>
#include <lal/LIGOMetadataUtils.h>
#include <lal/AVFactories.h>
#include <lal/NRWaveIO.h>
#include <lal/NRWaveInject.h>
#include <lal/LIGOLwXML.h>
#include <lal/LIGOLwXMLRead.h>
#include <lal/Inject.h>
#include <lal/FileIO.h>
#include <lal/Units.h>
#include <lal/FrequencySeries.h>
#include <lal/TimeSeries.h>
#include <lal/TimeFreqFFT.h>
#include <lal/VectorOps.h>
#include <lal/LALDetectors.h>
#include <lal/LALFrameIO.h>
#include <lal/UserInput.h>
#include <lalappsfrutils.h>
#include <lal/FrameStream.h>
#include <lal/LogPrintf.h>

#include <processtable.h>

RCSID( "$Id$" );

#define CVS_ID_STRING "$Id$"
#define CVS_NAME_STRING "$Name$"
#define CVS_REVISION "$Revision$"
#define CVS_SOURCE "$Source$"
#define CVS_DATE "$Date$"
#define PROGRAM_NAME "lalapps_ninja"


#define ADD_PROCESS_PARAM( pptype, format, ppvalue ) \
  this_proc_param = this_proc_param->next = (ProcessParamsTable *) \
calloc( 1, sizeof(ProcessParamsTable) ); \
LALSnprintf( this_proc_param->program, LIGOMETA_PROGRAM_MAX, "%s", \
    PROGRAM_NAME ); \
LALSnprintf( this_proc_param->param, LIGOMETA_PARAM_MAX, "--%s", \
    long_options[option_index].name ); \
LALSnprintf( this_proc_param->type, LIGOMETA_TYPE_MAX, "%s", pptype ); \
LALSnprintf( this_proc_param->value, LIGOMETA_VALUE_MAX, format, ppvalue );

/* function prototypes */


#define TRUE (1==1)
#define FALSE (1==0)

/* verbose flag */
extern int vrbflg;

typedef struct {
  REAL8 massRatioMin; 
  REAL8 massRatioMax;
  REAL8 sx1Min; 
  REAL8 sx1Max;
  REAL8 sx2Min; 
  REAL8 sx2Max;
  REAL8 sy1Min; 
  REAL8 sy1Max;
  REAL8 sy2Min; 
  REAL8 sy2Max;
  REAL8 sz1Min; 
  REAL8 sz1Max;
  REAL8 sz2Min; 
  REAL8 sz2Max; 
} NrParRange;


/* functions for internal use only */

int get_nr_metadata_from_framehistory(NRWaveMetaData *data, FrHistory *history);

int get_mode_index_from_channel_name(INT4  *mode_l, INT4  *mode_m, CHAR  *name);

int get_minmax_modes(INT4 *min, INT4 *max, FrameH *frame);

int get_eta_spins_from_string(NRWaveMetaData *data, CHAR *comment);

int metadata_in_range(NRWaveMetaData *data, NrParRange *range);


/* main program entry */
int main( INT4 argc, CHAR *argv[] )
{
  LALStatus status = blank_status;

  /* frame file stuff */
  FrCache *frGlobCache = NULL;
  FrCache *frInCache = NULL;
  FrCacheSieve  sieve;
  FrameH *frame=NULL;
  FrFile *frFile=NULL;

  /* inspiral table stuff */
  SimInspiralTable  *this_inj = NULL;
  LIGOLwXMLStream   xmlfp;
  MetadataTable  injections;
  MetadataTable proctable;
  MetadataTable procparams;
  ProcessParamsTable   *this_proc_param = NULL;
  LALLeapSecAccuracy  accuracy = LALLEAPSEC_LOOSE;


  /* nrwave stuff */
  NRWaveMetaData metaData;
  NrParRange range;

  UINT4 k;

  /* user input variables */
  BOOLEAN  uvar_help = FALSE;
  CHAR *uvar_nrDir=NULL;
  CHAR *uvar_nrGroup=NULL;
  CHAR *uvar_outFile=NULL;
  REAL8 uvar_minMassRatio=1, uvar_maxMassRatio=0;
  REAL8 uvar_minSx1=-1, uvar_minSx2=-1, uvar_maxSx1=1, uvar_maxSx2=1;
  REAL8 uvar_minSy1=-1, uvar_minSy2=-1, uvar_maxSy1=1, uvar_maxSy2=1;
  REAL8 uvar_minSz1=-1, uvar_minSz2=-1, uvar_maxSz1=1, uvar_maxSz2=1;


  /* default debug level */
  lal_errhandler = LAL_ERR_EXIT;

  lalDebugLevel = 0;
  LAL_CALL( LALGetDebugLevel( &status, argc, argv, 'd'), &status);

  uvar_outFile = (CHAR *)LALCalloc(1,257*sizeof(CHAR));
  strcpy(uvar_outFile, "ninja_out.xml");

  LAL_CALL( LALRegisterBOOLUserVar( &status, "help", 'h', UVAR_HELP, "Print this message", &uvar_help), &status);  
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "nrDir", 'D', UVAR_REQUIRED, "Directory with NR data", &uvar_nrDir), &status);

  LAL_CALL( LALRegisterSTRINGUserVar( &status, "outFile", 'o', UVAR_OPTIONAL, "Output xml filename", &uvar_outFile), &status);

  LAL_CALL( LALRegisterREALUserVar( &status, "minMassRatio", 0, UVAR_OPTIONAL, "Min. mass ratio", &uvar_minMassRatio),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "maxMassRatio", 0, UVAR_OPTIONAL, "Max. mass ratio", &uvar_maxMassRatio),  &status);

  LAL_CALL( LALRegisterREALUserVar( &status, "minSx1", 0, UVAR_OPTIONAL, "Min. x-spin of first BH", &uvar_minSx1),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "minSx2", 0, UVAR_OPTIONAL, "Min. x-Spin of second BH", &uvar_minSx2),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "maxSx1", 0, UVAR_OPTIONAL, "Max. x-spin of first BH", &uvar_maxSx1),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "maxSx2", 0, UVAR_OPTIONAL, "Max. x-spin of second BH", &uvar_maxSx2),  &status);

  LAL_CALL( LALRegisterREALUserVar( &status, "minSy1", 0, UVAR_OPTIONAL, "Min. y-spin of first BH", &uvar_minSy1),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "minSy2", 0, UVAR_OPTIONAL, "Min. y-Spin of second BH", &uvar_minSy2),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "maxSy1", 0, UVAR_OPTIONAL, "Max. y-spin of first BH", &uvar_maxSy1),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "maxSy2", 0, UVAR_OPTIONAL, "Max. y-spin of second BH", &uvar_maxSy2),  &status);

  LAL_CALL( LALRegisterREALUserVar( &status, "minSz1", 0, UVAR_OPTIONAL, "Min. z-spin of first BH", &uvar_minSz1),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "minSz2", 0, UVAR_OPTIONAL, "Min. z-Spin of second BH", &uvar_minSz2),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "maxSz1", 0, UVAR_OPTIONAL, "Max. z-spin of first BH", &uvar_maxSz1),  &status);
  LAL_CALL( LALRegisterREALUserVar( &status, "maxSz2", 0, UVAR_OPTIONAL, "Max. z-spin of second BH", &uvar_maxSz2),  &status);

  LAL_CALL( LALRegisterSTRINGUserVar( &status, "nrGroup", 0, UVAR_OPTIONAL, "NR group", &uvar_nrGroup), &status);

  /* read all command line variables */
  LAL_CALL( LALUserVarReadAllInput(&status, argc, argv), &status);

  /* exit if help was required */
  if (uvar_help)
    exit(0); 


  range.massRatioMin = uvar_minMassRatio;
  range.massRatioMax = uvar_maxMassRatio;

  range.sx1Min = uvar_minSx1;
  range.sx1Max = uvar_maxSx1;

  range.sx2Min = uvar_minSx2;
  range.sx2Max = uvar_maxSx2;

  range.sy1Min = uvar_minSy1;
  range.sy1Max = uvar_maxSy1;

  range.sy2Min = uvar_minSy2;
  range.sy2Max = uvar_maxSy2;

  range.sz1Min = uvar_minSz1;
  range.sz1Max = uvar_maxSz1;

  range.sz2Min = uvar_minSz2;
  range.sz2Max = uvar_maxSz2;



  LogPrintf (LOG_NORMAL, "Globbing frame files...");  

  /* create a frame cache by globbing *.gwf in specified dir */
  LAL_CALL( LALFrCacheGenerate( &status, &frGlobCache, uvar_nrDir, NULL ), 
	    &status );

  memset( &sieve, 0, sizeof(FrCacheSieve) );
  /* sieve doesn't actually do anything yet */
  LAL_CALL( LALFrCacheSieve( &status, &frInCache, frGlobCache, &sieve ), 
	    &status );

  LAL_CALL( LALDestroyFrCache( &status, &frGlobCache ), &status );  
  
  /* check we globbed at least one frame file */
  if ( !frInCache->numFrameFiles )  {
    fprintf( stderr, "error: no numrel frame files found\n");
    exit(1);
  }
  LogPrintfVerbatim (LOG_NORMAL, "found %d\n",frInCache->numFrameFiles);


  /* initialize head of simInspiralTable linked list to null */
  injections.simInspiralTable = NULL;

  LogPrintf (LOG_NORMAL, "Selecting frame files with right numrel parameters...");  
  /* loop over frame files and select the ones with nr-params in the right range */
  for (k = 0; k < frInCache->numFrameFiles; k++) {

    frFile =  XLALFrOpenURL( frInCache->frameFiles[k].url);

    frame = FrameRead (frFile);
    
    memset(&metaData, 0, sizeof(NRWaveMetaData));    
    get_nr_metadata_from_framehistory( &metaData, frame->history);

    /* if we find parameters in range then write to the siminspiral table */
    if ( metadata_in_range(&metaData, &range)) {

      REAL8 tmp;
      INT4 minMode, maxMode;
      
      /* alloc next element of inspiral table linked list*/
      if ( injections.simInspiralTable )   {
	this_inj = this_inj->next = (SimInspiralTable *)LALCalloc( 1, sizeof(SimInspiralTable) );
      }
      else  {
	injections.simInspiralTable = this_inj = (SimInspiralTable *)LALCalloc( 1, sizeof(SimInspiralTable) );
      }

      get_minmax_modes( &minMode,&maxMode,frame);

      /* eta = 1/(sqrt(mu) + 1/sqrt(mu))^2 where mu = m1/m2 */
      tmp = sqrt(metaData.massRatio) + 1.0/sqrt(metaData.massRatio);
      this_inj->eta = 1.0/( tmp * tmp );

      this_inj->spin1x = metaData.spin1[0];
      this_inj->spin1y = metaData.spin1[1];
      this_inj->spin1z = metaData.spin1[2];

      this_inj->spin2x = metaData.spin2[0];
      this_inj->spin2y = metaData.spin2[1];
      this_inj->spin2z = metaData.spin2[2];

      strcpy(this_inj->numrel_data,frInCache->frameFiles[k].url);

      this_inj->numrel_mode_min = minMode;
      this_inj->numrel_mode_max = maxMode;

    } /* end if (metadata is in range) */   

  } /* end loop over framefiles */
  LogPrintfVerbatim (LOG_NORMAL, "done\n");

  /* now write the output xml file */
  LogPrintf (LOG_NORMAL, "Writing xml output...");


  /* first the process table */
  proctable.processTable = (ProcessTable *)LALCalloc( 1, sizeof(ProcessTable) );
  LAL_CALL( LALGPSTimeNow ( &status, &(proctable.processTable->start_time), &accuracy ), &status );
  LAL_CALL( populate_process_table( &status, proctable.processTable, PROGRAM_NAME, CVS_REVISION, CVS_SOURCE, CVS_DATE ), &status );
  LALSnprintf( proctable.processTable->comment, LIGOMETA_COMMENT_MAX, " " );

  memset( &xmlfp, 0, sizeof(LIGOLwXMLStream) );
  LAL_CALL( LALOpenLIGOLwXMLFile( &status, &xmlfp, uvar_outFile ), &status );

  LAL_CALL( LALGPSTimeNow ( &status, &(proctable.processTable->end_time), &accuracy ), &status );
  LAL_CALL( LALBeginLIGOLwXMLTable( &status, &xmlfp, process_table ), &status );

  LAL_CALL( LALWriteLIGOLwXMLTable( &status, &xmlfp, proctable, process_table ), &status );
  LAL_CALL( LALEndLIGOLwXMLTable ( &status, &xmlfp ), &status );


  /* now the process params table */
  LAL_CALL( LALUserVarGetProcParamsTable ( &status, &this_proc_param, PROGRAM_NAME), &status);
  procparams.processParamsTable = this_proc_param;

  if ( procparams.processParamsTable )
  {
    LAL_CALL( LALBeginLIGOLwXMLTable( &status, &xmlfp, process_params_table ),
	      &status );
    LAL_CALL( LALWriteLIGOLwXMLTable( &status, &xmlfp, procparams, 
				      process_params_table ), &status );
    LAL_CALL( LALEndLIGOLwXMLTable ( &status, &xmlfp ), &status );
  }


  /* and finally the simInspiralTable itself */
  if ( injections.simInspiralTable )
  {
    LAL_CALL( LALBeginLIGOLwXMLTable( &status, &xmlfp, sim_inspiral_table ),
	      &status );
    LAL_CALL( LALWriteLIGOLwXMLTable( &status, &xmlfp, injections,
				      sim_inspiral_table ), &status );
    LAL_CALL( LALEndLIGOLwXMLTable ( &status, &xmlfp ), &status );
  }
  LogPrintfVerbatim (LOG_NORMAL, "done\n");


  /* we are now done with the xml writing stuff*/  
  /* free memory and exit */

  LogPrintf (LOG_NORMAL, "Free memory and exiting...");

  /* close the various xml tables */
  while ( injections.simInspiralTable )
  {
    this_inj = injections.simInspiralTable;
    injections.simInspiralTable = injections.simInspiralTable->next;
    LALFree( this_inj );
  }

  while ( procparams.processParamsTable )
  {
    this_proc_param = procparams.processParamsTable;
    procparams.processParamsTable = procparams.processParamsTable->next;
    LALFree( this_proc_param );
  }

  LALFree(proctable.processTable);

  /* close cache */
  /*   LAL_CALL( LALFrClose( &status, &frStream ), &status ); */
  LAL_CALL( LALDestroyFrCache( &status, &frInCache ), &status );  

  /* close the injection file */
  LAL_CALL( LALCloseLIGOLwXMLFile ( &status, &xmlfp ), &status );

  /* destroy all user input variables */
  LAL_CALL (LALDestroyUserVars(&status), &status);

  LALCheckMemoryLeaks();
  LogPrintfVerbatim (LOG_NORMAL, "bye\n");

  return 0;
} /* main */


/* metadata is stored in the history field comment 
   -- this function parses the comment to fill the metadata struct */
int get_nr_metadata_from_framehistory(NRWaveMetaData *data,
				      FrHistory *history)
{

  UINT4 strlen=128;
  CHAR *comment=NULL; /* the comments string */
  FrHistory *localhist;

  comment = LALMalloc(strlen*sizeof(CHAR));

  localhist = history;
  while (localhist) {

    strcpy(comment,localhist->comment);

    get_eta_spins_from_string(data, comment);

    localhist = localhist->next;
  }

  LALFree(comment);
  return 0;
}


int get_eta_spins_from_string(NRWaveMetaData *data,
			      CHAR *comment)
{

  CHAR *token;

  token = strtok(comment,":");

  if (strstr(token,"spin1x")) {
    token = strtok(NULL,":");
    data->spin1[0] = atof(token);
    return 0;
  }

  if (strstr(token,"spin1y")) {
    token = strtok(NULL,":");
    data->spin1[1] = atof(token);
    return 0;
  }

  if (strstr(token,"spin1z")) {
    token = strtok(NULL,":");
    data->spin1[2] = atof(token);
    return 0;
  }

  if (strstr(token,"spin2x")) {
    token = strtok(NULL,":");
    data->spin2[0] = atof(token);
    return 0;
  }

  if (strstr(token,"spin2y")) {
    token = strtok(NULL,":");
    data->spin2[1] = atof(token);
    return 0;
  }

  if (strstr(token,"spin2z")) {
    token = strtok(NULL,":");
    data->spin2[2] = atof(token);
    return 0;
  }

  if (strstr(token,"mass-ratio")) {
    token = strtok(NULL,":");
    data->massRatio = atof(token);
    return 0;
  }


  /* did not match anything */
  return -1;

}





int metadata_in_range(NRWaveMetaData *data, NrParRange *range)
{

  INT4 ret;
  BOOLEAN flag = FALSE;

  flag = (data->massRatio >= range->massRatioMin) && (data->massRatio <= range->massRatioMax);
  flag = flag && (data->spin1[0] >= range->sx1Min) && (data->spin1[0] <= range->sx1Max);
  flag = flag && (data->spin2[0] >= range->sx2Min) && (data->spin2[0] <= range->sx2Max);  
  flag = flag && (data->spin1[1] >= range->sy1Min) && (data->spin1[1] <= range->sy1Max);
  flag = flag && (data->spin2[1] >= range->sy2Min) && (data->spin2[1] <= range->sy2Max);
  flag = flag && (data->spin1[2] >= range->sz1Min) && (data->spin1[2] <= range->sz1Max);
  flag = flag && (data->spin2[2] >= range->sz2Min) && (data->spin2[2] <= range->sz2Max);

  if (flag) 
    ret = 1;
  else
    ret = 0; 

  return(ret);

}


int get_minmax_modes(INT4 *min,
		     INT4 *max,
		     FrameH *frame)
{
  int ret=1;
  INT4 mode_l, mode_m, locmin, locmax;
  FrSimData *sim;
  
  locmin = 10;
  locmax = 0;
  sim = frame->simData;
  while (sim) {
    if (!get_mode_index_from_channel_name( &mode_l, &mode_m, sim->name)) { 
      if (locmin > mode_l)
	locmin = mode_l;
      if (locmax < mode_l)
	locmax = mode_l;
    }

    sim = sim->next;
  }

  *min = locmin;
  *max = locmax;

  return ret;
}

/* very hackish -- need to make this better */
int get_mode_index_from_channel_name(INT4  *mode_l,
				     INT4  *mode_m,
				     CHAR  *name)
{
  int ret=1;
  CHAR *tmp;
  INT4 sign=0;

  tmp = strstr(name, "hcross_");
  if (tmp) {
    
    tmp += strlen("hcross_") + 1;
    *mode_l = atoi(tmp);
    tmp = strstr(tmp,"m");
    tmp++;

    if (!strncmp(tmp,"p",1))
      sign = 1;

    if (!strncmp(tmp,"n",1))
      sign = -1;
    
    tmp++;
    *mode_m = sign*atoi(tmp);
    ret = 0;

  }


  tmp = strstr(name, "hplus_");
  if (tmp) {

    tmp += strlen("hplus_") + 1;
    *mode_l = atoi(tmp);
    tmp = strstr(tmp,"m");
    tmp++;

    if (!strncmp(tmp,"p",1))
      sign = 1;

    if (!strncmp(tmp,"n",1))
      sign = -1;
    
    tmp++;
    *mode_m = sign*atoi(tmp);
    
    ret = 0;
  }



  return ret;

}
