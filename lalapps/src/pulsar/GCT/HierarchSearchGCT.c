/*  
 *  Copyright (C) 2009 Holger Pletsch.
 *
 *  Based on HierarchicalSearch.c by
 *  Copyright (C) 2005-2008 Badri Krishnan, Alicia Sintes, Bernd Machenschalk.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with with program; see the file COPYING. If not, write to the 
 *  Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *  MA  02111-1307  USA
 * 
 */

/*********************************************************************************/
/** \author Holger Pletsch
 * \file
 * \brief Hierarchical semicoherent CW search code based on F-Statistic,
 *  exploiting global-correlation coordinates (Phys.Rev.Lett. 103, 181102, 2009)
 *
 *********************************************************************************/

/* ---------- Includes -------------------- */
#include <lal/lalGitID.h>
#include <lalappsGitID.h>
#include "HierarchSearchGCT.h"

RCSID( "$Id$");

/* ---------- Defines -------------------- */
#define TRUE (1==1)
#define FALSE (1==0)

/* Hooks for Einstein@Home / BOINC
   These are defined to do nothing special in the standalone case
   and will be set in boinc_extras.h if EAH_BOINC is set
 */
#ifdef EAH_BOINC
#include "hs_boinc_extras.h"
#else
#define HS_CHECKPOINTING 0 /* no checkpointing in the non-BOINC case (yet) */
#define GET_CHECKPOINT(toplist,total,count,outputname,cptname) *total=0;
#define INSERT_INTO_GCTFSTAT_TOPLIST insert_into_gctFStat_toplist
#define SHOW_PROGRESS(rac,dec,tpl_count,tpl_total,freq,fband)
#define SET_CHECKPOINT
#define MAIN  main
#define FOPEN fopen
#define COMPUTEFSTATFREQBAND ComputeFStatFreqBand
#define COMPUTEFSTATFREQBAND_RS ComputeFStatFreqBand_RS
#endif

#define EARTHEPHEMERIS  "earth05-09.dat"
#define SUNEPHEMERIS 		"sun05-09.dat"
#define BLOCKSRNGMED 		101 	/**< Default running median window size */
#define FSTART 			    100.0	/**< Default Start search frequency */
#define FBAND           0.01  /**< Default search band */
#define FDOT            0.0	  /**< Default value of first spindown */
#define DFDOT           0.0	  /**< Default range of first spindown parameter */
#define SKYREGION       "allsky" /**< default sky region to search over -- just a single point*/
#define DTERMS          16     	/**< Default number of dirichlet kernel terms for calculating Fstat */
#define MISMATCH        0.3 	  /**< Default for metric grid maximal mismatch value */
#define DALPHA          0.001 	/**< Default resolution for isotropic or flat grids */
#define DDELTA          0.001 	/**< Default resolution for isotropic or flat grids */
#define FSTATTHRESHOLD 	2.6	/**< Default threshold on Fstatistic for peak selection */
#define NCAND1          10 	/**< Default number of candidates to be followed up from first stage */
#define FNAMEOUT        "./HS_GCT.out"  /**< Default output file basename */
#ifndef LAL_INT4_MAX
#define LAL_INT4_MAX 		2147483647
#endif

#define BLOCKSIZE_REALLOC 50


/* ---------- Macros -------------------- */
#define HSMAX(x,y) ( (x) > (y) ? (x) : (y) )
#define HSMIN(x,y) ( (x) < (y) ? (x) : (y) )
#define INIT_MEM(x) memset(&(x), 0, sizeof((x)))


/* ---------- Exported types ---------- */
/** useful variables for each hierarchical stage */
typedef struct {
  CHAR  *sftbasename;    /**< filename pattern for sfts */
  REAL8 tStack;          /**< duration of stacks */
  UINT4 nStacks;         /**< number of stacks */
  LIGOTimeGPS tStartGPS; /**< start and end time of stack */
  REAL8 tObs;            /**< tEndGPS - tStartGPS */
  REAL8 refTime;         /**< reference time for pulsar params */
  PulsarSpinRange spinRange_startTime; /**< freq and fdot range at start-time of observation */
  PulsarSpinRange spinRange_endTime;   /**< freq and fdot range at end-time of observation */
  PulsarSpinRange spinRange_refTime;   /**< freq and fdot range at the reference time */
  PulsarSpinRange spinRange_midTime;   /**< freq and fdot range at mid-time of observation */
  EphemerisData *edat;             /**< ephemeris data for LALBarycenter */
  LIGOTimeGPSVector *midTstack;    /**< timestamps vector for mid time of each stack */ 
  LIGOTimeGPSVector *startTstack;  /**< timestamps vector for start time of each stack */ 
  LIGOTimeGPSVector *endTstack;    /**< timestamps vector for end time of each stack */ 
  LIGOTimeGPS minStartTimeGPS;     /**< all sft data must be after this time */
  LIGOTimeGPS maxEndTimeGPS;       /**< all sft data must be before this time */
  UINT4 blocksRngMed;              /**< blocksize for running median noise floor estimation */
  UINT4 Dterms;                    /**< size of Dirichlet kernel for Fstat calculation */
  BOOLEAN SignalOnly;              /**< FALSE: estimate noise-floor from data, TRUE: assume Sh=1 */
  REAL8 dopplerMax;                /**< extra sft wings for doppler motion */
} UsefulStageVariables;


/* ------------------------ Functions -------------------------------- */
void SetUpSFTs( LALStatus *status, MultiSFTVectorSequence *stackMultiSFT, 
               MultiNoiseWeightsSequence *stackMultiNoiseWeights,
               MultiDetectorStateSeriesSequence *stackMultiDetStates, UsefulStageVariables *in );
void PrintCatalogInfo( LALStatus *status, const SFTCatalog *catalog, FILE *fp );
void PrintStackInfo( LALStatus *status, const SFTCatalogSequence *catalogSeq, FILE *fp );
void GetSemiCohToplist( LALStatus *status, toplist_t *list, FineGrid *in, UsefulStageVariables *usefulparams );
void TranslateFineGridSpins( LALStatus *status, UsefulStageVariables *usefulparams, FineGrid *in);
void GetSegsPosVelAccEarthOrb( LALStatus *status, REAL8VectorSequence **posSeg, 
                              REAL8VectorSequence **velSeg, REAL8VectorSequence **accSeg, 
                              UsefulStageVariables *usefulparams );
void ComputeU1idx( LALStatus *status, const REAL8 *f_event, const REAL8 *f1dot_event, 
                  const REAL8 *A1, const REAL8 *B1, const REAL8 *U1start, const REAL8 *U1winInv, 
                  INT4 *U1idx );
void ComputeU2idx( LALStatus *status, const REAL8 *f_event, const REAL8 *f1dot_event, 
                  const REAL8 *A2, const REAL8 *B2, const REAL8 *U2start, const REAL8 *U2winInv,
                  INT4 *U2idx);
int compareCoarseGridUindex( const void *a, const void *b );
int compareFineGridUindex( const void *a,const void *b );
int compareFineGridNC( const void *a,const void *b );
int compareFineGridsumTwoF( const void *a,const void *b );
void OutputVersion( void );

/* ---------- Global variables -------------------- */
LALStatus *global_status; /* a global pointer to MAIN()s head of the LALStatus structure */
extern int lalDebugLevel;

#ifdef OUTPUT_TIMING
time_t clock0;
UINT4 nSFTs;
UINT4 nStacks;
UINT4 nSkyRefine;
#endif


/* ###################################  MAIN  ################################### */

int MAIN( int argc, char *argv[]) {

  LALStatus status = blank_status;

  /* temp loop variables: generally k loops over segments and j over SFTs in a stack*/
  INT4 j;
  UINT4 k;
  UINT4 skyGridCounter; /* coarse sky position counter */

  /* ephemeris */
  EphemerisData *edat = NULL;

  /* GPS timestamp vectors */
  LIGOTimeGPSVector *midTstack=NULL; 
  LIGOTimeGPSVector *startTstack=NULL; 
  LIGOTimeGPSVector *endTstack=NULL;  
  
  /* General GPS times */
  LIGOTimeGPS refTimeGPS = empty_LIGOTimeGPS;
  LIGOTimeGPS tStartGPS = empty_LIGOTimeGPS; 
  LIGOTimeGPS tMidGPS = empty_LIGOTimeGPS;

  /* GPS times used for each segment */
  LIGOTimeGPS startTstackGPS = empty_LIGOTimeGPS;
  LIGOTimeGPS midTstackGPS = empty_LIGOTimeGPS;
  LIGOTimeGPS endTstackGPS = empty_LIGOTimeGPS;
  REAL8 midTseg, midTobs, timeDiffSeg, startTseg, endTseg, refTimeFstat;
  
  /* pos, vel, acc at midpoint of segments */
  REAL8VectorSequence *posStack = NULL;
  REAL8VectorSequence *velStack = NULL;
  REAL8VectorSequence *accStack = NULL; 
	
	/* duration of each segment */
  REAL8 tStack;

  /* number of segments */
  UINT4 nStacks;
  
  /* Total observation time */
  REAL8 tObs;
  
  /* SFT related stuff */
  static MultiSFTVectorSequence stackMultiSFT;
  static MultiNoiseWeightsSequence stackMultiNoiseWeights;
  static MultiDetectorStateSeriesSequence stackMultiDetStates;
  static LIGOTimeGPS minStartTimeGPS, maxEndTimeGPS;
  SFTtype *firstSFT;
  REAL8 Tsft;
  
  /* some useful variables for each stage */
  UsefulStageVariables usefulParams;

  /* F-statistic computation related stuff */
  REAL4FrequencySeriesVector fstatVector; /* F-statistic vectors for each segment */
  UINT4 binsFstat1, binsFstatSearch;
  static ComputeFParams CFparams;		   

  /* Semicoherent variables */
  static SemiCoherentParams semiCohPar;
  static SemiCohCandidateList semiCohCandList;
  
  /* coarse grid */
  CoarseGrid coarsegrid;
  CoarseGridPoint thisCgPoint; /* a single coarse-grid point */
  REAL8 dFreqStack; /* frequency resolution of Fstat calculation */
  REAL8 df1dot;  /* coarse grid resolution in spindown */
  UINT4 nf1dot;  /* number of coarse-grid spindown values */
  UINT4 ifdot;  /* counter for coarse-grid spindown values */
  
  /* fine grid */
  FineGrid finegrid;
  FineGridPoint thisFgPoint; /* a single fine-grid point */
  UINT4 nfreqsFG, nf1dotsFG; /* number of frequency and spindown values */
  REAL8 gamma2, sigmasq;  /* refinement factor and variance */
  
  /* GCT helper variables */
  UINT4 ic, ic2, ic3, ifine;
  INT4 fveclength, ifreq, U1idx, U2idx, NumU2idx;
  REAL8 myf0, myf0max, f_event, f1dot_event, deltaF;
  REAL8 fg_freq_step, fg_f1dot_step, fg_fmin,f1dotmin, fg_fband, fg_f1dotband;
  REAL8 u1win, u2win, u1winInv, u2winInv, u1fac, u2fac;
  REAL8 u1start, u2start, u2end;
  REAL8 f_tmp, f1dot_tmp;
  REAL8 TwoFthreshold;
  REAL8 TwoFmax;
  UINT4 nc_max;
  REAL8 pos[3];
  REAL8 vel[3];
  REAL8 acc[3];
  REAL8 Fstat=0.0;
  REAL8 A1, B1, A2, B2; /* GCT helper variables for faster calculation of u1, u2 */
       
  
  /* fstat candidate structure */
  toplist_t *semiCohToplist=NULL;

  /* template and grid variables */
  static DopplerSkyScanInit scanInit;   /* init-structure for DopperScanner */
  DopplerSkyScanState thisScan = empty_DopplerSkyScanState; /* current state of the Doppler-scan */
  static PulsarDopplerParams dopplerpos;	       /* current search-parameters */
  static PulsarDopplerParams thisPoint; 

  /* temporary storage for spinrange vector */
  static PulsarSpinRange spinRange_Temp;

  /* variables for logging */
  CHAR *fnamelog=NULL;
  FILE *fpLog=NULL;
  CHAR *logstr=NULL; 

  /* output candidate files and file pointers */
  CHAR *fnameSemiCohCand=NULL;
  CHAR *fnameFstatVec1=NULL;
  FILE *fpSemiCoh=NULL;
  FILE *fpFstat1=NULL;
  
  /* checkpoint filename and index of loop over skypoints */
  /* const CHAR *fnameChkPoint="checkpoint.cpt"; */
  /*   FILE *fpChkPoint=NULL; */
  /*   UINT4 loopindex, loopcounter; */
  
  /* user variables */
  BOOLEAN uvar_help = FALSE; 	/* true if -h option is given */
  BOOLEAN uvar_log = FALSE; 	/* logging done if true */

  BOOLEAN uvar_printCand1 = FALSE; 	/* if 1st stage candidates are to be printed */
  BOOLEAN uvar_printFstat1 = FALSE;
  BOOLEAN uvar_useToplist1 = FALSE;
  BOOLEAN uvar_semiCohToplist = FALSE; /* if overall first stage candidates are to be output */
  BOOLEAN uvar_useResamp = FALSE;      /* use resampling to compute F-statistic instead of SFT method */
  BOOLEAN uvar_SignalOnly = FALSE;     /* if Signal-only case (for SFT normalization) */
  
  REAL8 uvar_dAlpha = DALPHA; 	/* resolution for flat or isotropic grids -- coarse grid*/
  REAL8 uvar_dDelta = DDELTA; 		
  REAL8 uvar_f1dot = FDOT; 	/* first spindown value */
  REAL8 uvar_f1dotBand = DFDOT; /* range of first spindown parameter */
  REAL8 uvar_Freq = FSTART;
  REAL8 uvar_FreqBand = FBAND;

  REAL8 uvar_dFreq = 0; 
  REAL8 uvar_df1dot = 0; /* coarse grid frequency and spindown resolution */

  REAL8 uvar_ThrF = FSTATTHRESHOLD; /* threshold of Fstat to select peaks */
  REAL8 uvar_mismatch1 = MISMATCH; /* metric mismatch for first stage coarse grid */

  REAL8 uvar_threshold1 = 0.0;
  REAL8 uvar_minStartTime1 = 0;
  REAL8 uvar_maxEndTime1 = LAL_INT4_MAX;
  REAL8 uvar_dopplerMax = 1.05e-4;

  REAL8 uvar_refTime = 0;
  REAL8 uvar_tStack = 0;
  INT4 uvar_nCand1 = NCAND1; /* number of candidates to be followed up from first stage */

  INT4 uvar_blocksRngMed = BLOCKSRNGMED;
  INT4 uvar_nStacksMax = 1;
  INT4 uvar_Dterms = DTERMS;
  INT4 uvar_SSBprecision = SSBPREC_RELATIVISTIC;
  INT4 uvar_gamma2 = 1;
  INT4 uvar_metricType1 = LAL_PMETRIC_COH_PTOLE_ANALYTIC;
  INT4 uvar_gridType1 = GRID_METRIC;
  INT4 uvar_sftUpsampling = 1;
  INT4 uvar_skyPointIndex = -1;

  CHAR *uvar_ephemE = NULL;
  CHAR *uvar_ephemS = NULL;

  CHAR *uvar_skyRegion = NULL;
  CHAR *uvar_fnameout = NULL;
  CHAR *uvar_DataFiles1 = NULL;
  CHAR *uvar_skyGridFile=NULL;
  INT4 uvar_numSkyPartitions = 0;
  INT4 uvar_partitionIndex = 0;

  global_status = &status;


  /* LALDebugLevel must be called before any LALMallocs have been used */
  lalDebugLevel = 0;
  LAL_CALL( LALGetDebugLevel( &status, argc, argv, 'd'), &status);
#ifdef EAH_LALDEBUGLEVEL
  lalDebugLevel = EAH_LALDEBUGLEVEL;
#endif

  uvar_ephemE = LALCalloc( strlen( EARTHEPHEMERIS ) + 1, sizeof(CHAR) );
  strcpy(uvar_ephemE, EARTHEPHEMERIS);

  uvar_ephemS = LALCalloc( strlen(SUNEPHEMERIS) + 1, sizeof(CHAR) );
  strcpy(uvar_ephemS, SUNEPHEMERIS);

  uvar_skyRegion = LALCalloc( strlen(SKYREGION) + 1, sizeof(CHAR) );
  strcpy(uvar_skyRegion, SKYREGION);

  uvar_fnameout = LALCalloc( strlen(FNAMEOUT) + 1, sizeof(CHAR) );
  strcpy(uvar_fnameout, FNAMEOUT);

  /* set LAL error-handler */
#ifdef EAH_BOINC
  lal_errhandler = BOINC_LAL_ErrHand;
#else
  lal_errhandler = LAL_ERR_EXIT;
#endif

  /* register user input variables */
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "help",        'h', UVAR_HELP,     "Print this message", &uvar_help), &status);  
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "log",          0,  UVAR_OPTIONAL, "Write log file", &uvar_log), &status);  
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "semiCohToplist",0, UVAR_OPTIONAL, "Print semicoh toplist?", &uvar_semiCohToplist ), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "DataFiles1",   0,  UVAR_REQUIRED, "1st SFT file pattern", &uvar_DataFiles1), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "skyRegion",    0,  UVAR_OPTIONAL, "sky-region polygon (or 'allsky')", &uvar_skyRegion), &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "numSkyPartitions",0,UVAR_OPTIONAL, "Number of (equi-)partitions to split skygrid into", &uvar_numSkyPartitions), &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "partitionIndex",0,UVAR_OPTIONAL, "Index [0,numSkyPartitions-1] of sky-partition to generate", &uvar_partitionIndex), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "Freq",        'f', UVAR_OPTIONAL, "Start search frequency", &uvar_Freq), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "dFreq",        0,  UVAR_OPTIONAL, "Frequency resolution (default=1/Tstack)", &uvar_dFreq), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "FreqBand",    'b', UVAR_OPTIONAL, "Search frequency band", &uvar_FreqBand), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "f1dot",        0,  UVAR_OPTIONAL, "Spindown parameter", &uvar_f1dot), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "df1dot",       0,  UVAR_OPTIONAL, "Spindown resolution (default=1/Tstack^2)", &uvar_df1dot), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "f1dotBand",    0,  UVAR_OPTIONAL, "Spindown Range", &uvar_f1dotBand), &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "nStacksMax",   0,  UVAR_OPTIONAL, "Maximum No. of 1st stage segments", &uvar_nStacksMax ),&status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "tStack",      'T', UVAR_REQUIRED, "Duration of 1st stage segments (sec)", &uvar_tStack ),&status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "mismatch1",   'm', UVAR_OPTIONAL, "1st stage mismatch", &uvar_mismatch1), &status);
  LAL_CALL( LALRegisterINTUserVar (   &status, "gridType1",    0,  UVAR_OPTIONAL, "0=flat,1=isotropic,2=metric,3=file", &uvar_gridType1),  &status);
  LAL_CALL( LALRegisterINTUserVar (   &status, "metricType1",  0,  UVAR_OPTIONAL, "0=none,1=Ptole-analytic,2=Ptole-numeric,3=exact", &uvar_metricType1), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "skyGridFile",  0,  UVAR_OPTIONAL, "sky-grid file", &uvar_skyGridFile), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "dAlpha",       0,  UVAR_OPTIONAL, "Resolution for flat or isotropic coarse grid", &uvar_dAlpha), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "dDelta",       0,  UVAR_OPTIONAL, "Resolution for flat or isotropic coarse grid", &uvar_dDelta), &status);
  LAL_CALL( LALRegisterINTUserVar (   &status, "gamma2",      'g', UVAR_OPTIONAL, "Refinement of spindown in fine grid (default: use segment times)", &uvar_gamma2), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "fnameout",    'o', UVAR_OPTIONAL, "Output fileneme", &uvar_fnameout), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "peakThrF",     0,  UVAR_OPTIONAL, "Fstat Threshold", &uvar_ThrF), &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "nCand1",      'n', UVAR_OPTIONAL, "No. of candidates to output", &uvar_nCand1), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "threshold1",   0,  UVAR_OPTIONAL, "Threshold (if no toplist)", &uvar_threshold1), &status);
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "printCand1",   0,  UVAR_OPTIONAL, "Print 1st stage candidates", &uvar_printCand1), &status);  
  LAL_CALL( LALRegisterREALUserVar(   &status, "refTime",      0,  UVAR_OPTIONAL, "Ref. time for pulsar pars [Default: mid-time]", &uvar_refTime), &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "ephemE",       0,  UVAR_OPTIONAL, "Location of Earth ephemeris file", &uvar_ephemE),  &status);
  LAL_CALL( LALRegisterSTRINGUserVar( &status, "ephemS",       0,  UVAR_OPTIONAL, "Location of Sun ephemeris file", &uvar_ephemS),  &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "minStartTime1",0,  UVAR_OPTIONAL, "1st stage min start time of observation", &uvar_minStartTime1), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "maxEndTime1",  0,  UVAR_OPTIONAL, "1st stage max end time of observation",   &uvar_maxEndTime1),   &status);
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "printFstat1",  0,  UVAR_OPTIONAL, "Print 1st stage Fstat vectors", &uvar_printFstat1), &status);  
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "useResamp",    0,  UVAR_OPTIONAL, "Use resampling to compute F-statistic", &uvar_useResamp), &status);  
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "SignalOnly",  'S', UVAR_OPTIONAL, "Signal only flag", &uvar_SignalOnly), &status);
  
  /* developer user variables */
  LAL_CALL( LALRegisterINTUserVar(    &status, "blocksRngMed", 0, UVAR_DEVELOPER, "RngMed block size", &uvar_blocksRngMed), &status);
  LAL_CALL( LALRegisterINTUserVar (   &status, "SSBprecision", 0, UVAR_DEVELOPER, "Precision for SSB transform.", &uvar_SSBprecision),    &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "Dterms",       0, UVAR_DEVELOPER, "No.of terms to keep in Dirichlet Kernel", &uvar_Dterms ), &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "skyPointIndex",0, UVAR_DEVELOPER, "Only analyze this skypoint in grid", &uvar_skyPointIndex ), &status);
  LAL_CALL( LALRegisterREALUserVar(   &status, "dopplerMax",   0, UVAR_DEVELOPER, "Max Doppler shift",  &uvar_dopplerMax), &status);
  LAL_CALL( LALRegisterINTUserVar(    &status, "sftUpsampling",0, UVAR_DEVELOPER, "Upsampling factor for fast LALDemod",  &uvar_sftUpsampling), &status);
  LAL_CALL( LALRegisterBOOLUserVar(   &status, "useToplist1",  0, UVAR_DEVELOPER, "Use toplist for 1st stage candidates?", &uvar_useToplist1 ), &status);

  /* read all command line variables */
  LAL_CALL( LALUserVarReadAllInput(&status, argc, argv), &status);

  /* exit if help was required */
  if (uvar_help)
    return(0); 

  /* set log-level */
#ifdef EAH_LOGLEVEL
  LogSetLevel ( EAH_LOGLEVEL );
#else
  LogSetLevel ( lalDebugLevel );
#endif
  
  /* assemble version string */
  CHAR *version_string;
  {
    CHAR *id1, *id2;
    id1 = XLALClearLinebreaks ( lalGitID );
    id2 = XLALClearLinebreaks ( lalappsGitID );
    UINT4 len = strlen ( id1 ) + strlen ( id2 ) + 20;
    if ( ( version_string = XLALMalloc ( len )) == NULL ) {
      XLALPrintError ("Failed to XLALMalloc ( %d ).\n", len );
      return( HIERARCHICALSEARCH_EMEM );
    }
    sprintf (version_string, "%%%% %s\n%%%% %s\n", id1, id2 );
    XLALFree ( id1 );
    XLALFree ( id2 );
  }
  LogPrintfVerbatim( LOG_DEBUG, "Code-version: %s", version_string );

  /* some basic sanity checks on user vars */
  if ( uvar_nStacksMax < 1) {
    fprintf(stderr, "Invalid number of segments!\n");
    return( HIERARCHICALSEARCH_EBAD );
  }

  if ( uvar_blocksRngMed < 1 ) {
    fprintf(stderr, "Invalid Running Median block size\n");
    return( HIERARCHICALSEARCH_EBAD );
  }

  if ( uvar_ThrF < 0 ) {
    fprintf(stderr, "Invalid value of Fstatistic threshold\n");
    return( HIERARCHICALSEARCH_EBAD );
  }
  
  /* 2F threshold for semicoherent stage */
  TwoFthreshold = 2.0 * uvar_ThrF;
    
  /* create toplist -- semiCohToplist has the same structure 
     as a fstat candidate, so treat it as a fstat candidate */
  create_gctFStat_toplist(&semiCohToplist, uvar_nCand1);

  /* write the log file */
  if ( uvar_log ) 
    {
      fnamelog = LALCalloc( strlen(uvar_fnameout) + 1 + 4, sizeof(CHAR) );
      strcpy(fnamelog, uvar_fnameout);
      strcat(fnamelog, ".log");
      /* open the log file for writing */
      if ((fpLog = fopen(fnamelog, "wb")) == NULL) {
        fprintf(stderr, "Unable to open file %s for writing\n", fnamelog);
        LALFree(fnamelog);
        /*exit*/ 
        return(HIERARCHICALSEARCH_EFILE);
      }

      /* get the log string */
      LAL_CALL( LALUserVarGetLog(&status, &logstr, UVAR_LOGFMT_CFGFILE), &status);  
      
      fprintf( fpLog, "## Log file for HierarchSearchGCT.c\n\n");
      fprintf( fpLog, "# User Input:\n");
      fprintf( fpLog, "#-------------------------------------------\n");
      fprintf( fpLog, "# cmdline: %s\n", logstr );
      LALFree(logstr);
      
      /* add code version ID (only useful for git-derived versions) */
      fprintf ( fpLog, "# version: %s\n", version_string );
      
      fclose (fpLog);
      LALFree(fnamelog);
      
    } /* end of logging */
  
  /* initializations of coarse and fine grids */
  coarsegrid.list = NULL;
  finegrid.list = NULL;

  /* initialize ephemeris info */ 
  edat = (EphemerisData *)LALCalloc(1, sizeof(EphemerisData));
  if ( edat == NULL) {
    fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
    return(HIERARCHICALSEARCH_EMEM);
  }
  (*edat).ephiles.earthEphemeris = uvar_ephemE;
  (*edat).ephiles.sunEphemeris = uvar_ephemS;
  
  /* read in ephemeris data */
  LAL_CALL( LALInitBarycenter( &status, edat), &status);        

  XLALGPSSetREAL8(&minStartTimeGPS, uvar_minStartTime1);
  XLALGPSSetREAL8(&maxEndTimeGPS, uvar_maxEndTime1);
    
  /* create output files for writing if requested by user */
  if ( uvar_printCand1 )
    {
      fnameSemiCohCand = LALCalloc( strlen(uvar_fnameout) + 1, sizeof(CHAR) );
      if ( fnameSemiCohCand == NULL) {
        fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
        return(HIERARCHICALSEARCH_EMEM);
      }
      strcpy(fnameSemiCohCand, uvar_fnameout);
    }
  
  if ( uvar_printFstat1 )
    {
      const CHAR *append = "_fstatVec1.dat";
      fnameFstatVec1 = LALCalloc( strlen(uvar_fnameout) + strlen(append) + 1, sizeof(CHAR) );
      strcpy(fnameFstatVec1, uvar_fnameout);
      strcat(fnameFstatVec1, append);
      if ( !(fpFstat1 = fopen( fnameFstatVec1, "wb"))) 	{
        fprintf ( stderr, "Unable to open Fstat file fstatvec1.out for writing.\n");
        return (HIERARCHICALSEARCH_EFILE);
      }
    }

  /*------------ Set up stacks, detector states etc. */
  /* initialize spin range vectors */
  INIT_MEM( spinRange_Temp );

  /* some useful first stage params */
  usefulParams.sftbasename = uvar_DataFiles1;
  usefulParams.nStacks = uvar_nStacksMax;
  usefulParams.tStack = uvar_tStack;

  INIT_MEM ( usefulParams.spinRange_startTime );
  INIT_MEM ( usefulParams.spinRange_endTime );
  INIT_MEM ( usefulParams.spinRange_refTime );
  INIT_MEM ( usefulParams.spinRange_midTime );

  /* copy user specified spin variables at reftime  */
  /* the reference time value in spinRange_refTime will be set in SetUpSFTs() */
  usefulParams.spinRange_refTime.fkdot[0] = uvar_Freq; /* frequency */
  usefulParams.spinRange_refTime.fkdot[1] = uvar_f1dot;  /* 1st spindown */
  usefulParams.spinRange_refTime.fkdotBand[0] = uvar_FreqBand; /* frequency range */
  usefulParams.spinRange_refTime.fkdotBand[1] = uvar_f1dotBand; /* spindown range */

  usefulParams.edat = edat;
  usefulParams.minStartTimeGPS = minStartTimeGPS;
  usefulParams.maxEndTimeGPS = maxEndTimeGPS;
  usefulParams.blocksRngMed = uvar_blocksRngMed;
  usefulParams.Dterms = uvar_Dterms;
  usefulParams.SignalOnly = uvar_SignalOnly;
  usefulParams.dopplerMax = uvar_dopplerMax;

  /* set reference time for pulsar parameters */
  if ( LALUserVarWasSet(&uvar_refTime)) 
    usefulParams.refTime = uvar_refTime;
  else {
    LogPrintf(LOG_DETAIL, "Reference time will be set to mid-time of observation time\n");
    usefulParams.refTime = -1;
  }
  
  /* for 1st stage: read sfts, calculate detector states */  
  /*LogPrintf (LOG_DEBUG, "Reading SFTs and setting up segments ... ");*/
  fprintf(stderr,"%% --- Reading input data...");
  LAL_CALL( SetUpSFTs( &status, &stackMultiSFT, &stackMultiNoiseWeights, &stackMultiDetStates, &usefulParams), &status);
  /*LogPrintfVerbatim (LOG_DEBUG, "done\n");*/
  fprintf(stderr," done.\n");  


  /* some useful params computed by SetUpSFTs */
  tStack = usefulParams.tStack;
  tObs = usefulParams.tObs;
  nStacks = usefulParams.nStacks;
  tStartGPS = usefulParams.tStartGPS;
  midTstack = usefulParams.midTstack;
  startTstack = usefulParams.startTstack;
  endTstack = usefulParams.endTstack;
  tMidGPS = usefulParams.spinRange_midTime.refTime;
  refTimeGPS = usefulParams.spinRange_refTime.refTime;
  fprintf(stderr, "%% --- GPS reference time = %d   GPS data mid time = %d\n", 
          refTimeGPS.gpsSeconds, tMidGPS.gpsSeconds);
  firstSFT = &(stackMultiSFT.data[0]->data[0]->data[0]); /* use  first SFT from  first detector */
  Tsft = 1.0 / firstSFT->deltaF; /* define the length of an SFT (assuming 1/Tsft resolution) */
  
  if ( uvar_sftUpsampling > 1 )
    {
      LogPrintf (LOG_DEBUG, "Upsampling SFTs by factor %d ... ", uvar_sftUpsampling );
      for (k = 0; k < nStacks; k++) {
        LAL_CALL ( upsampleMultiSFTVector ( &status, stackMultiSFT.data[k], uvar_sftUpsampling, 16 ), &status );
      }
      LogPrintfVerbatim (LOG_DEBUG, "done.\n");
    }

  /*------- set frequency and spindown resolutions and ranges for Fstat and semicoherent steps -----*/

  /* set Fstat calculation frequency resolution (coarse grid) */
  if ( LALUserVarWasSet(&uvar_dFreq) ) {
    dFreqStack = uvar_dFreq;
  }
  else {
    dFreqStack = 1.0/tStack;
  }

  /* set Fstat spindown resolution (coarse grid) */ 
  if ( LALUserVarWasSet(&uvar_df1dot) ) {
    df1dot = uvar_df1dot;
  }
  else {
    df1dot = 1.0/(tStack*tStack);
  }

  /* number of coarse grid spindown values */
  nf1dot = (UINT4)( usefulParams.spinRange_midTime.fkdotBand[1] / df1dot + 1e-6) + 1; 

  /* set number of fine-grid spindowns */
  if ( LALUserVarWasSet(&uvar_gamma2) ) {
    gamma2 = uvar_gamma2;
  }
  else {
    
    midTobs = XLALGPSGetREAL8( &tMidGPS ); /* midpoint of whole data set is tMidGPS */
    sigmasq=0.0;
    for (k = 0; k < nStacks; k++) {
      
      midTstackGPS = midTstack->data[k];
      midTseg = XLALGPSGetREAL8( &midTstackGPS );

      timeDiffSeg = midTseg - midTobs;  
      sigmasq = sigmasq + (timeDiffSeg * timeDiffSeg);
      
    }
    
    sigmasq = sigmasq / (nStacks * tStack * tStack);
    gamma2 = sqrt(1.0 + 60 * sigmasq);
  }
  fprintf(stderr, "%% --- Refinement factor, gamma = %f\n", gamma2);

 

  /**** debugging information ******/   
  /* print some debug info about spinrange */
  LogPrintf(LOG_DETAIL, "Frequency and spindown range at refTime (%d): [%f-%f], [%e-%e]\n", 
	    usefulParams.spinRange_refTime.refTime.gpsSeconds,
	    usefulParams.spinRange_refTime.fkdot[0], 
	    usefulParams.spinRange_refTime.fkdot[0] + usefulParams.spinRange_refTime.fkdotBand[0], 
	    usefulParams.spinRange_refTime.fkdot[1], 
	    usefulParams.spinRange_refTime.fkdot[1] + usefulParams.spinRange_refTime.fkdotBand[1]);
  
  LogPrintf(LOG_DETAIL, "Frequency and spindown range at startTime (%d): [%f-%f], [%e-%e]\n", 
	    usefulParams.spinRange_startTime.refTime.gpsSeconds,
	    usefulParams.spinRange_startTime.fkdot[0], 
	    usefulParams.spinRange_startTime.fkdot[0] + usefulParams.spinRange_startTime.fkdotBand[0], 
	    usefulParams.spinRange_startTime.fkdot[1], 
	    usefulParams.spinRange_startTime.fkdot[1] + usefulParams.spinRange_startTime.fkdotBand[1]);

  LogPrintf(LOG_DETAIL, "Frequency and spindown range at midTime (%d): [%f-%f], [%e-%e]\n", 
	    usefulParams.spinRange_midTime.refTime.gpsSeconds,
	    usefulParams.spinRange_midTime.fkdot[0], 
	    usefulParams.spinRange_midTime.fkdot[0] + usefulParams.spinRange_midTime.fkdotBand[0], 
	    usefulParams.spinRange_midTime.fkdot[1], 
	    usefulParams.spinRange_midTime.fkdot[1] + usefulParams.spinRange_midTime.fkdotBand[1]);

  LogPrintf(LOG_DETAIL, "Frequency and spindown range at endTime (%d): [%f-%f], [%e-%e]\n", 
	    usefulParams.spinRange_endTime.refTime.gpsSeconds,
	    usefulParams.spinRange_endTime.fkdot[0], 
	    usefulParams.spinRange_endTime.fkdot[0] + usefulParams.spinRange_endTime.fkdotBand[0], 
	    usefulParams.spinRange_endTime.fkdot[1], 
	    usefulParams.spinRange_endTime.fkdot[1] + usefulParams.spinRange_endTime.fkdotBand[1]);

  /* print debug info about stacks */
  LogPrintf(LOG_DETAIL, "1st stage params: Nstacks = %d,  Tstack = %.0fsec, dFreq = %eHz, Tobs = %.0fsec\n", 
	    nStacks, tStack, dFreqStack, tObs);
  for (k = 0; k < nStacks; k++) {

    LogPrintf(LOG_DETAIL, "Segment %d ", k);
    for ( j = 0; j < (INT4)stackMultiSFT.data[k]->length; j++) {

      INT4 tmpVar = stackMultiSFT.data[k]->data[j]->length;
      LogPrintfVerbatim(LOG_DETAIL, "%s: %d  ", stackMultiSFT.data[k]->data[j]->data[0].name, tmpVar);
    } /* loop over ifos */    

    LogPrintfVerbatim(LOG_DETAIL, "\n");
    
  } /* loop over segments */



  /*---------- set up F-statistic calculation stuff ---------*/
  
  /* set reference time for calculating Fstatistic */
  thisPoint.refTime = tMidGPS; /* midpoint of data spanned */
  
  /* binary orbit and higher spindowns not considered */
  thisPoint.orbit = NULL;
  thisPoint.fkdot[2] = 0.0;
  thisPoint.fkdot[3] = 0.0;

  /* some compute F-Stat params */
  CFparams.Dterms = uvar_Dterms;
  CFparams.SSBprec = uvar_SSBprecision;
  CFparams.upsampling = uvar_sftUpsampling;
  CFparams.edat = edat;
  
  /* set up some semiCoherent parameters */
  semiCohPar.useToplist = uvar_useToplist1;
  semiCohPar.tsMid = midTstack;
  semiCohPar.refTime = tMidGPS;

  /* allocate memory for pos vel acc vectors */
  posStack = XLALCreateREAL8VectorSequence( nStacks, 3 );
  velStack = XLALCreateREAL8VectorSequence( nStacks, 3 );
  accStack = XLALCreateREAL8VectorSequence( nStacks, 3 );
  
  /* calculate Earth orbital positions, velocities and accelerations */
  LAL_CALL( GetSegsPosVelAccEarthOrb( &status, &posStack, &velStack, &accStack, &usefulParams), &status);
 
  /* semicoherent configuration parameters */
  semiCohPar.pos = posStack;
  semiCohPar.vel = velStack;
  semiCohPar.acc = accStack;
  semiCohPar.outBaseName = uvar_fnameout;
  semiCohPar.gamma2 = gamma2;

  /* allocate memory for semicoherent candidates */
  semiCohCandList.length = uvar_nCand1;
  semiCohCandList.refTime = tMidGPS;
  semiCohCandList.nCandidates = 0; /* initialization */
  semiCohCandList.list = (SemiCohCandidate *)LALCalloc( 1, semiCohCandList.length * sizeof(SemiCohCandidate));
  if ( semiCohCandList.list == NULL) {
    fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
    return(HIERARCHICALSEARCH_EMEM);
  }

  /* allocate some fstat memory */
  fstatVector.length = nStacks; /* for EACH segment generate a fstat-vector */
  fstatVector.data = NULL;
  fstatVector.data = (REAL4FrequencySeries *)LALCalloc( 1, nStacks * sizeof(REAL4FrequencySeries)); 
  if ( fstatVector.data == NULL) {
    fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
    return(HIERARCHICALSEARCH_EMEM);
  }
  

  /*-----------Create template grid for first stage ---------------*/
  /* prepare initialization of DopplerSkyScanner to step through paramter space */
  scanInit.dAlpha = uvar_dAlpha;
  scanInit.dDelta = uvar_dDelta;
  scanInit.gridType = uvar_gridType1;
  scanInit.metricType = uvar_metricType1;
  scanInit.metricMismatch = uvar_mismatch1;
  scanInit.projectMetric = TRUE;
  scanInit.obsDuration = tStack;
  scanInit.obsBegin = tMidGPS;
  scanInit.Detector = &(stackMultiDetStates.data[0]->data[0]->detector);
  scanInit.ephemeris = edat;
  scanInit.skyGridFile = uvar_skyGridFile;
  scanInit.skyRegionString = (CHAR*)LALCalloc(1, strlen(uvar_skyRegion)+1);
  if ( scanInit.skyRegionString == NULL) {
    fprintf(stderr, "error allocating memory [HierarchSearchGCT.c.c %d]\n" , __LINE__);
    return(HIERARCHICALSEARCH_EMEM);
  }
  strcpy (scanInit.skyRegionString, uvar_skyRegion);

  scanInit.numSkyPartitions = uvar_numSkyPartitions;
  scanInit.partitionIndex = uvar_partitionIndex;

  scanInit.Freq = usefulParams.spinRange_midTime.fkdot[0] +  usefulParams.spinRange_midTime.fkdotBand[0];

  /* initialize skygrid  */  
  LogPrintf(LOG_DETAIL, "Setting up coarse sky grid...");
  LAL_CALL ( InitDopplerSkyScan ( &status, &thisScan, &scanInit), &status); 
  LogPrintfVerbatim(LOG_DETAIL, "done\n");  

  
  /* ----- start main calculations by going over coarse grid points --------*/
 
  /* ########## loop over SKY coarse-grid points ########## */
  skyGridCounter = 0;

  XLALNextDopplerSkyPos(&dopplerpos, &thisScan);
  
  /* "spool forward" if we found a checkpoint */
  {
    UINT4 count = 0;
    GET_CHECKPOINT(semiCohToplist, &count, thisScan.numSkyGridPoints, fnameSemiCohCand, NULL);
    for(skyGridCounter = 0; skyGridCounter < count; skyGridCounter++)
      XLALNextDopplerSkyPos(&dopplerpos, &thisScan);
  }

  /* spool forward if uvar_skyPointIndex is set
     ---This probably doesn't make sense when checkpointing is turned on */
  if ( LALUserVarWasSet(&uvar_skyPointIndex)) {
    UINT4 count = uvar_skyPointIndex;
    for(skyGridCounter = 0; (skyGridCounter < count)&&(thisScan.state != STATE_FINISHED) ; skyGridCounter++)
      XLALNextDopplerSkyPos(&dopplerpos, &thisScan);
  }

  
#ifdef SKYPOS_PRECISION
  LogPrintf(LOG_DEBUG, "SKYPOS_PRECISION: %15f (0x%x)\n", (REAL4)SKYPOS_PRECISION,(INT8)SKYPOS_PRECISION);
#endif

  LogPrintf(LOG_DEBUG, "Total skypoints = %d. Progress: ", thisScan.numSkyGridPoints);

#ifdef OUTPUT_TIMING
    clock0 = time(NULL);
#endif 


  while(thisScan.state != STATE_FINISHED) {
    
      SkyPosition skypos;

      /* if (skyGridCounter == 25965) { */

#ifdef SKYPOS_PRECISION
      /* reduce precision of sky position */
      dopplerpos.Alpha = (INT4)(dopplerpos.Alpha * SKYPOS_PRECISION) / (REAL4)SKYPOS_PRECISION;
      dopplerpos.Delta = (INT4)(dopplerpos.Delta * SKYPOS_PRECISION) / (REAL4)SKYPOS_PRECISION;
#endif

      SHOW_PROGRESS(dopplerpos.Alpha,dopplerpos.Delta,
		    skyGridCounter,thisScan.numSkyGridPoints,
		    uvar_Freq, uvar_FreqBand);

      
      /*------------- calculate F-Statistic for each segment --------------*/
      
      /* normalize skyposition: correctly map into [0,2pi]x[-pi/2,pi/2] */
      thisPoint.Alpha = dopplerpos.Alpha;
      thisPoint.Delta = dopplerpos.Delta;   
      
      /* get amplitude modulation weights */
      skypos.longitude = thisPoint.Alpha;
      skypos.latitude = thisPoint.Delta;
      skypos.system = COORDINATESYSTEM_EQUATORIAL;
      
      {  /********Allocate fstat vector memory *****************/
	
        /* extra bins for F-Statistic due to spin-down */
        REAL8 freqHighest;
	
        /* calculate number of bins for fstat overhead */
        freqHighest = usefulParams.spinRange_midTime.fkdot[0] + usefulParams.spinRange_midTime.fkdotBand[0];
        semiCohPar.extraBinsFstat=ceil(gamma2);
	
        /* allocate fstat memory */
        binsFstatSearch = (UINT4)(usefulParams.spinRange_midTime.fkdotBand[0]/dFreqStack + 1e-6) + 1;
        binsFstat1 = binsFstatSearch + 2*semiCohPar.extraBinsFstat;
	
        /* loop over segments */
        for (k = 0; k < nStacks; k++) { 
                  
          /* watch out: the epoch here is not the reference time for f0! */
          fstatVector.data[k].epoch = startTstack->data[k];
          fstatVector.data[k].deltaF = dFreqStack;
          fstatVector.data[k].f0 = usefulParams.spinRange_midTime.fkdot[0] - semiCohPar.extraBinsFstat * dFreqStack;
          
          if (fstatVector.data[k].data == NULL) {
            fstatVector.data[k].data = (REAL4Sequence *)LALCalloc( 1, sizeof(REAL4Sequence));
            if ( fstatVector.data[k].data == NULL) {
              fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
              return(HIERARCHICALSEARCH_EMEM);
            }

            fstatVector.data[k].data->length = binsFstat1;
            fstatVector.data[k].data->data = (REAL4 *)LALCalloc( 1, binsFstat1 * sizeof(REAL4));
            if ( fstatVector.data[k].data->data == NULL) {
              fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
              return(HIERARCHICALSEARCH_EMEM);
            }

          } 
          else {
            fstatVector.data[k].data = (REAL4Sequence *)LALRealloc( fstatVector.data[k].data, sizeof(REAL4Sequence));
            if ( fstatVector.data[k].data == NULL) {
              fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
              return(HIERARCHICALSEARCH_EMEM);
            }

            fstatVector.data[k].data->length = binsFstat1;
            fstatVector.data[k].data->data = (REAL4 *)LALRealloc( fstatVector.data[k].data->data, binsFstat1 * sizeof(REAL4));
            if ( fstatVector.data[k].data->data == NULL) {
              fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
              return(HIERARCHICALSEARCH_EMEM);
            }
          } 
        } /* loop over segments */
      } /* fstat memory allocation block */


      /* ########## loop over F-Statistic calculation on coarse-grid f1dot values ########## */
      for (ifdot = 0; ifdot < nf1dot; ifdot++) { 

        LogPrintfVerbatim(LOG_DEBUG, "\n%% --- Sky point: %d / %d   Spin-down: %d / %d\n", 
                        skyGridCounter+1, thisScan.numSkyGridPoints, ifdot+1, nf1dot );
      
        fprintf(stderr, "%% --- Progress, coarse-grid sky point: %d / %d  and spin-down: %d / %d\n", 
                skyGridCounter+1, thisScan.numSkyGridPoints, ifdot+1, nf1dot ); 
        
        /* ------------- Set up coarse grid --------------------------------------*/
        coarsegrid.length = (UINT4) (binsFstat1);
        LogPrintf(LOG_DEBUG, "Coarse-grid points in frequency per segment = %d\n",coarsegrid.length);
     
        /* allocate memory for coarsegrid */
        coarsegrid.list = (CoarseGridPoint *)LALRealloc( coarsegrid.list, coarsegrid.length * sizeof(CoarseGridPoint));
        if ( coarsegrid.list == NULL) {
          fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
          return(HIERARCHICALSEARCH_EMEM);
        }

        /* Initialize first coarsegrid point */
        thisCgPoint.Index=0;
        thisCgPoint.Uindex=0;
        thisCgPoint.TwoF=0.0;
      

        /* ------------- Set up fine grid --------------------------------------*/

        /* fine-grid borders */
        fg_fmin = usefulParams.spinRange_midTime.fkdot[0];
        fg_fband = usefulParams.spinRange_midTime.fkdotBand[0];
        f1dotmin = usefulParams.spinRange_midTime.fkdot[1] + ifdot * df1dot;
        fg_f1dotband = usefulParams.spinRange_midTime.fkdotBand[1];

        /* fine-grid freq resoultion */
        fg_freq_step = dFreqStack;
        nfreqsFG = ceil(fg_fband / fg_freq_step);  /* number of points in frequency */
      
        /* fine-grid f1dot resoultion */
        nf1dotsFG = ceil(gamma2);        /* number of spindown fine-grid  points */
        if ( (nf1dotsFG % 2) == 0 ) {    /* if even, add one */
          nf1dotsFG++;
        }
        fg_f1dot_step = df1dot / nf1dotsFG;  /* spindown fine-grid  stepsize */
    
        /* adjust f1dotmin to be centered around coarse-grid f1dot point */
        f1dotmin = f1dotmin - fg_f1dot_step * floor(nf1dotsFG / 2.0);

        /* total number of fine-grid points */
        finegrid.length = nf1dotsFG * nfreqsFG;
        LogPrintf(LOG_DEBUG, "Total number of finegrid points = %ld\n",finegrid.length);

        /* reference time for finegrid */
        finegrid.refTime = tMidGPS;

        /* allocate memory for finegrid points */
        finegrid.list = (FineGridPoint *)LALRealloc( finegrid.list, finegrid.length * sizeof(FineGridPoint));
        if ( finegrid.list == NULL) {
          fprintf(stderr, "error allocating memory [HierarchSearchGCT.c %d]\n" , __LINE__);
          return(HIERARCHICALSEARCH_EMEM);
        }
      
        /* copy sky coarse-grid point to finegrid, because sky is not refined */
        finegrid.Alpha = thisPoint.Alpha;
        finegrid.Delta = thisPoint.Delta;

        /* initialize first finegrid point */
        thisFgPoint.F=0.0;
        thisFgPoint.F1dot=0.0;
        thisFgPoint.Index=0;
        thisFgPoint.Uindex=0;
        thisFgPoint.nc=0;
        thisFgPoint.sumTwoF=0.0;

        /* initialize the entire finegrid */
        ic=0;
        f_tmp = fg_fmin;
        for(ic2=0;ic2<nfreqsFG;ic2++) {
          f1dot_tmp = f1dotmin;
          for(ic3=0;ic3<nf1dotsFG;ic3++) {
              thisFgPoint.F     = f_tmp;
              thisFgPoint.F1dot = f1dot_tmp;
              thisFgPoint.Index = ic;
              finegrid.list[ic] = thisFgPoint;
              f1dot_tmp = f1dot_tmp + fg_f1dot_step;
              ic++;
          }
          f_tmp = f_tmp + fg_freq_step;
        }

        /* --------------------------------------------------------------- */
      
        /* U-map configuration */
        u1fac = 1.0; /* default */
        u2fac = 1.0;

        /* Keeping track of maximum number count */
        nc_max = 0;    /* initialize */
        TwoFmax = 0.0; 
        
      
        /* ##########################################################*/
        /* ------------- MAIN LOOP over Segments --------------------*/
      
        for (k = 0; k < nStacks; k++) {
        
          /* Get pos vel acc for this segment */
          pos[0] = semiCohPar.pos->data[3*k];
          pos[1] = semiCohPar.pos->data[3*k + 1];
          pos[2] = semiCohPar.pos->data[3*k + 2];
          
          vel[0] = semiCohPar.vel->data[3*k];
          vel[1] = semiCohPar.vel->data[3*k + 1];
          vel[2] = semiCohPar.vel->data[3*k + 2];
          
          acc[0] = semiCohPar.acc->data[3*k];
          acc[1] = semiCohPar.acc->data[3*k + 1];
          acc[2] = semiCohPar.acc->data[3*k + 2];
          
          /* reference time for Fstat has to be midpoint of segment */
          startTstackGPS = startTstack->data[k];
          midTstackGPS   = midTstack->data[k];
          endTstackGPS   = endTstack->data[k];
          startTseg = XLALGPSGetREAL8( &startTstackGPS );
          midTseg = XLALGPSGetREAL8( &midTstackGPS );
          endTseg = XLALGPSGetREAL8( &endTstackGPS );
          refTimeFstat = XLALGPSGetREAL8( &thisPoint.refTime );
          
          /* Differentce in time between this segment's midpoint and Fstat reftime */
          timeDiffSeg = midTseg - refTimeFstat;   
                
          /* ---------------------------------------------------------------------------------------- */ 
          
          SHOW_PROGRESS(dopplerpos.Alpha,dopplerpos.Delta, skyGridCounter + (REAL4)k / (REAL4)nStacks, 
                        thisScan.numSkyGridPoints, uvar_Freq, uvar_FreqBand);

          /* Compute sky position associated dot products (for global-correlation coordinates) */
          
          A1 =  1.0 + ( vel[0] * cos(thisPoint.Alpha) * cos(thisPoint.Delta) \
                      + vel[1] * sin(thisPoint.Alpha) * cos(thisPoint.Delta) \
                      + vel[2] * sin(thisPoint.Delta) );
          
          B1 = ( pos[0] * cos(thisPoint.Alpha) * cos(thisPoint.Delta) \
               + pos[1] * sin(thisPoint.Alpha) * cos(thisPoint.Delta) \
               + pos[2] * sin(thisPoint.Delta) );
          
          A2 = ( acc[0] * cos(thisPoint.Alpha) * cos(thisPoint.Delta) \
               + acc[1] * sin(thisPoint.Alpha) * cos(thisPoint.Delta) \
               + acc[2] * sin(thisPoint.Delta) );
          
          B2 = ( vel[0] * cos(thisPoint.Alpha) * cos(thisPoint.Delta) \
               + vel[1] * sin(thisPoint.Alpha) * cos(thisPoint.Delta) \
               + vel[2] * sin(thisPoint.Delta) );
          
          /* Setup the tolerance windows in the U-map */
          u1win = u1fac * dFreqStack * A1;
          u2win = u2fac * df1dot;

          /* Take the inverse before the hot loop */
          u1winInv = 1.0/u1win;
          u2winInv = 1.0/u2win;
                
          /* ----------------------------------------------------------------- */ 
          /************************ Compute F-Statistic ************************/         
          
          /* Set starting frequency for Fstat calculation */
          thisPoint.fkdot[0] = fstatVector.data[k].f0; 
          
          /* Set spindown value for Fstat calculation */
          thisPoint.fkdot[1] = usefulParams.spinRange_midTime.fkdot[1] + ifdot * df1dot;
          
          /* Translate frequency to segment's midpoint for later use */
          f1dot_event = thisPoint.fkdot[1];
          myf0 = thisPoint.fkdot[0] + thisPoint.fkdot[1] * timeDiffSeg;
          fveclength = fstatVector.data[k].data->length;
          deltaF = fstatVector.data[k].deltaF;
          myf0max = thisPoint.fkdot[0] + (fveclength - 1) * deltaF + thisPoint.fkdot[1] * timeDiffSeg;
          
          
          if (uvar_useResamp) {
            
            /* Resampling method implementation to compute the F-statistic */
            LAL_CALL( COMPUTEFSTATFREQBAND_RS ( &status, &fstatVector.data[k], &thisPoint, 
                                               stackMultiSFT.data[k], stackMultiNoiseWeights.data[k], 
                                               stackMultiDetStates.data[k], &CFparams), &status);
          }
          else {

            /* SFT method implementation to compute the F-statistic */
            LAL_CALL( COMPUTEFSTATFREQBAND ( &status, &fstatVector.data[k], &thisPoint,
                                            stackMultiSFT.data[k], stackMultiNoiseWeights.data[k],
                                            stackMultiDetStates.data[k], &CFparams), &status);
          }
          
          
          /* Smallest values of u1 and u2 (to be subtracted) */
          u1start = myf0 * A1 + f1dot_event * B1;
          u2start = f1dot_event + myf0 * A2 + 2.0 * f1dot_event * B2;
          u2end = f1dot_event + myf0max * A2 + 2.0 * f1dot_event * B2;
          NumU2idx = ceil(fabs(u2start - u2end) * u2winInv); 
                  
          /* Initialize indices */
          U1idx = 0;
          U2idx = 0;
          
          /* Loop over frequency bins */
          for (ifreq = 0; ifreq < fveclength; ifreq++) {
                    
            Fstat = fstatVector.data[k].data->data[ifreq];

            if ( uvar_SignalOnly )
            {
              /* Correct normalization in --SignalOnly case:
               * we didn't normalize data by 1/sqrt(Tsft * 0.5 * Sh) in terms of
               * the single-sided PSD Sh: the SignalOnly case is characterized by
               * setting Sh->1, so we need to divide F by (0.5*Tsft)
               */
              Fstat *= 2.0 / Tsft;
              Fstat += 2;		/* compute E[2F]:= 4 + SNR^2 */
              fstatVector.data[k].data->data[ifreq] = Fstat;
            }
            
            /* translate frequency from midpoint of data span to midpoint of this segment */
            f_event = myf0 + ifreq * deltaF;
              
            /* compute the global-correlation coordinate indices */
            LAL_CALL( ComputeU1idx ( &status, &f_event, &f1dot_event, &A1, &B1, 
                                    &u1start, &u1winInv, &U1idx), &status);
            
            LAL_CALL( ComputeU2idx ( &status, &f_event, &f1dot_event, &A2, &B2, 
                                    &u2start, &u2winInv, &U2idx), &status);
            
            /* Check U1 index value */
            if ( ifreq != U1idx ) {
              fprintf(stderr, "WARNING:  Incorrect Frequency-Index!\n ----> Seg: %03d  ifreq: %d   cg U1: %d  cg U2: %d \n", 
                                k, ifreq, U1idx, U2idx);
              return(HIERARCHICALSEARCH_ECG);
            }
            else {
              thisCgPoint.Uindex = U1idx * NumU2idx + U2idx;
            }

            /* copy the *2F* value and index integer number */
            thisCgPoint.TwoF = 2.0 * Fstat;
            thisCgPoint.Index = ifreq;
            coarsegrid.list[ifreq] = thisCgPoint;
            
          }

          /* --- Sort the coarse grid in Uindex --- */
          qsort(coarsegrid.list, (size_t)coarsegrid.length, sizeof(CoarseGridPoint), compareCoarseGridUindex);          
          
          /* ---------------------------------------------------------------------------------------- */ 
          
          /* ---------- Compute finegrid U-map --------------- */
          for (ifine = 0; ifine < finegrid.length; ifine++) {

            /* translate frequency from midpoint of data span to midpoint of this segment */
            f_tmp = finegrid.list[ifine].F + finegrid.list[ifine].F1dot * timeDiffSeg;
           
            f1dot_tmp = finegrid.list[ifine].F1dot;
      
            /* compute the global-correlation coordinate indices */
            LAL_CALL( ComputeU1idx ( &status, &f_tmp, &f1dot_tmp, &A1, &B1, 
                                    &u1start, &u1winInv, &U1idx), &status);
            
            LAL_CALL( ComputeU2idx ( &status, &f_tmp, &f1dot_tmp, &A2, &B2, 
                                    &u2start, &u2winInv, &U2idx), &status);
            
            finegrid.list[ifine].Uindex = U1idx * NumU2idx + U2idx;
                        
            /* map coarse-grid to appropriate fine-grid points */
            
            if ( (U1idx >= 0) && (U1idx < fveclength) ) { /* consider only relevant frequency values */
              
              /*if (finegrid.list[ifine].Uindex == coarsegrid.list[finegrid.list[ifine].Uindex].Uindex) {*/
                
                /* Add the 2F value to the 2F sum */
                finegrid.list[ifine].sumTwoF = finegrid.list[ifine].sumTwoF + coarsegrid.list[finegrid.list[ifine].Uindex].TwoF;
                
                /* Increase the number count */
                if (coarsegrid.list[finegrid.list[ifine].Uindex].TwoF >= TwoFthreshold) { 
                  finegrid.list[ifine].nc++;
                }
                
                /* Find strongest candidate (maximum 2F sum and number count) */
                if(finegrid.list[ifine].nc > nc_max) {
                  nc_max = finegrid.list[ifine].nc;
                }
                if(finegrid.list[ifine].sumTwoF > TwoFmax) {
                  TwoFmax = finegrid.list[ifine].sumTwoF;
                }
                
                /* Select special template in fine grid */
                /*
                 if(finegrid.list[ifine].Index == 5206950 ) {
                 nc_max = finegrid.list[ifine].nc;
                 TwoFmax = finegrid.list[ifine].sumTwoF;
                 TwoFtemp = coarsegrid.list[finegrid.list[ifine].U1i].TwoF;
                 }
                 */
              /*}*/
            }   
            
            /* sort out the crap --- this should be replaced by something BETTER! */
            if ((finegrid.list[ifine].sumTwoF > 1.0e20) || (finegrid.list[ifine].sumTwoF < 0.0)) {
              finegrid.list[ifine].sumTwoF=-1.0;
            }   
                        
          } /* for (ifine = 0; ifine < finegrid.length; ifine++) { */
          
          LogPrintf(LOG_DETAIL, "  --- Seg: %03d  nc_max: %03d  sumTwoFmax: %f \n", k, nc_max, TwoFmax); 

        } /* end ------------- MAIN LOOP over Segments --------------------*/
        /* ############################################################### */

         
        /* check if translation to reference time of pulsar spins is necessary */
        if ( LALUserVarWasSet(&uvar_refTime) ) {
         if  ( finegrid.refTime.gpsSeconds != usefulParams.spinRange_refTime.refTime.gpsSeconds ) {
           LAL_CALL( TranslateFineGridSpins(&status, &usefulParams, &finegrid), &status); 
         }
        }
        
        if( uvar_semiCohToplist) {
          /* this is necessary here, because GetSemiCohToplist() might set
           a checkpoint that needs some information from here */
          SHOW_PROGRESS(dopplerpos.Alpha,dopplerpos.Delta, \
                        skyGridCounter,thisScan.numSkyGridPoints, \
                        uvar_Freq, uvar_FreqBand);
	    
          LogPrintf(LOG_DETAIL, "Selecting toplist from semicoherent candidates\n");
          LAL_CALL( GetSemiCohToplist(&status, semiCohToplist, &finegrid, &usefulParams), &status); 
        }
	  
      } /* ########## End of loop over coarse-grid f1dot values (ifdot) ########## */
       
      
      /* continue forward till the end if uvar_skyPointIndex is set
         ---This probably doesn't make sense when checkpointing is turned on */
      if ( LALUserVarWasSet(&uvar_skyPointIndex) ) {
        while(thisScan.state != STATE_FINISHED) {
          skyGridCounter++;
          XLALNextDopplerSkyPos(&dopplerpos, &thisScan);
        }
      }
      else {      
        skyGridCounter++;
  
        /* this is necessary here, because the checkpoint needs some information from here */
        SHOW_PROGRESS(dopplerpos.Alpha,dopplerpos.Delta, \
                    skyGridCounter,thisScan.numSkyGridPoints, \
                    uvar_Freq, uvar_FreqBand);
  
        SET_CHECKPOINT;
  
        XLALNextDopplerSkyPos( &dopplerpos, &thisScan );
      }

  } /* ######## End of while loop over 1st stage SKY coarse-grid points ############ */
  /*---------------------------------------------------------------------------------*/
  
#ifdef OUTPUT_TIMING
  {
    time_t tau = time(NULL) - clock0;
    UINT4 Nrefine = nSkyRefine * gamma2;
    FILE *timing_fp = fopen ( "HS_timing.dat", "ab" );
    fprintf ( timing_fp, "%d 	%d 	%d 	%d 	%d 	%d 	%d 	%d\n",  
	      thisScan.numSkyGridPoints, nf1dot, binsFstatSearch, 2 * semiCohPar.extraBinsFstat, nSFTs, nStacks, Nrefine, tau );
    fclose ( timing_fp );
  }
#endif
  

  LogPrintfVerbatim ( LOG_DEBUG, " ... done.\n");
  
  fprintf(stderr, "%% --- Finished analysis.\n");
  
  LogPrintf ( LOG_DEBUG, "Writing output ...");

#if (!HS_CHECKPOINTING)
  /* print candidates */  
  {
    if (!(fpSemiCoh = fopen(fnameSemiCohCand, "wb"))) {
      LogPrintf ( LOG_CRITICAL, "Unable to open output-file '%s' for writing.\n", fnameSemiCohCand);
      return HIERARCHICALSEARCH_EFILE;
    }
    if ( uvar_printCand1 && uvar_semiCohToplist ) {
      
      sort_gctFStat_toplist(semiCohToplist);
      
      if ( write_gctFStat_toplist_to_fp( semiCohToplist, fpSemiCoh, NULL) < 0) {
        fprintf( stderr, "Error in writing toplist to file\n");
      }
      
      if (fprintf(fpSemiCoh,"%%DONE\n") < 0) {
        fprintf(stderr, "Error writing end marker\n");
      }
      
      fclose(fpSemiCoh);
    }
  }
#else
  write_and_close_checkpointed_file();
#endif

  LogPrintfVerbatim ( LOG_DEBUG, " done.\n");
  

  /*------------ free all remaining memory -----------*/
  
  if ( uvar_printCand1 ) {
    LALFree( fnameSemiCohCand );
  }
  if ( version_string ) {
    XLALFree ( version_string );
  }
  if ( uvar_printFstat1 ) {
    fclose(fpFstat1);
    LALFree( fnameFstatVec1 );
  }

  /* free first stage memory */
  for ( k = 0; k < nStacks; k++) {
    LAL_CALL( LALDestroyMultiSFTVector ( &status, stackMultiSFT.data + k), &status);
    LAL_CALL( LALDestroyMultiNoiseWeights ( &status, stackMultiNoiseWeights.data + k), &status);
    XLALDestroyMultiDetectorStateSeries ( stackMultiDetStates.data[k] );
  }
  
  LALFree(stackMultiSFT.data);
  LALFree(stackMultiNoiseWeights.data);
  LALFree(stackMultiDetStates.data);

  XLALDestroyTimestampVector(startTstack);
  XLALDestroyTimestampVector(midTstack);
  XLALDestroyTimestampVector(endTstack);
  
  /* free Fstat vectors  */
  for(k = 0; k < nStacks; k++)
    if (fstatVector.data[k].data) {
      if (fstatVector.data[k].data->data)
        LALFree(fstatVector.data[k].data->data);
      LALFree(fstatVector.data[k].data);
    }
  LALFree(fstatVector.data);
  
  
  /* if resampling is used then free buffer */
  /*
  if ( uvar_useResamp ) {
    XLALEmptyComputeFBuffer_RS( CFparams.buffer );
  }
  */
  
  /* free Vel/Pos/Acc vectors and ephemeris */
  XLALDestroyREAL8VectorSequence( posStack );
  XLALDestroyREAL8VectorSequence( velStack );
  XLALDestroyREAL8VectorSequence( accStack );
  LALFree(edat->ephemE);
  LALFree(edat->ephemS);
  LALFree(edat);

  /* free dopplerscan stuff */
  LAL_CALL ( FreeDopplerSkyScan(&status, &thisScan), &status);
  if ( scanInit.skyRegionString )
    LALFree ( scanInit.skyRegionString );

  /* free fine grid and coarse grid */
  LALFree(finegrid.list);
  LALFree(coarsegrid.list);
 
  /* free candidates */
  LALFree(semiCohCandList.list);
  free_gctFStat_toplist(&semiCohToplist);

  LAL_CALL (LALDestroyUserVars(&status), &status);  

  LALCheckMemoryLeaks();

  return HIERARCHICALSEARCH_ENORM;
} /* main */







/** Set up stacks, read SFTs, calculate SFT noise weights and calculate 
    detector-state */
void SetUpSFTs( LALStatus *status,
		MultiSFTVectorSequence *stackMultiSFT, /**< output multi sft vector for each stack */
		MultiNoiseWeightsSequence *stackMultiNoiseWeights, /**< output multi noise weights for each stack */
		MultiDetectorStateSeriesSequence *stackMultiDetStates, /**< output multi detector states for each stack */
		UsefulStageVariables *in /**< input params */)
{

  SFTCatalog *catalog = NULL;
  static SFTConstraints constraints;
  REAL8 timebase, tObs, deltaFsft;
  UINT4 k,numSFT;
  LIGOTimeGPS tStartGPS, tEndGPS, refTimeGPS, tMidGPS, midTstackGPS, startTstackGPS, endTstackGPS;
  SFTCatalogSequence catalogSeq;
  REAL8 midTseg,startTseg,endTseg;
  
  REAL8 doppWings, freqmin, freqmax;
  REAL8 startTime_freqLo, startTime_freqHi;
  REAL8 endTime_freqLo, endTime_freqHi;
  REAL8 freqLo, freqHi;
  INT4 extraBins;
  
  INT4 sft_check_result = 0;

  INITSTATUS( status, "SetUpSFTs", rcsid );
  ATTATCHSTATUSPTR (status);

  /* get sft catalog */
  constraints.startTime = &(in->minStartTimeGPS);
  constraints.endTime = &(in->maxEndTimeGPS);
  TRY( LALSFTdataFind( status->statusPtr, &catalog, in->sftbasename, &constraints), status);

  /* check CRC sums of SFTs */
  TRY ( LALCheckSFTCatalog ( status->statusPtr, &sft_check_result, catalog ), status );
  if (sft_check_result) {
    LogPrintf(LOG_CRITICAL,"SFT validity check failed (%d)\n", sft_check_result);
    ABORT ( status, HIERARCHICALSEARCH_ESFT, HIERARCHICALSEARCH_MSGESFT );
  }

  /* set some sft parameters */
  deltaFsft = catalog->data[0].header.deltaF;
  timebase = 1.0/deltaFsft;
  
  /* calculate start and end times and tobs from catalog*/
  tStartGPS = catalog->data[0].header.epoch;
  in->tStartGPS = tStartGPS;
  tEndGPS = catalog->data[catalog->length - 1].header.epoch;
  XLALGPSAdd(&tEndGPS, timebase);
  tObs = XLALGPSDiff(&tEndGPS, &tStartGPS);
  in->tObs = tObs;

  /* get sft catalogs for each stack */
  TRY( SetUpStacks( status->statusPtr, &catalogSeq, in->tStack, catalog, in->nStacks), status);

  /* reset number of stacks */
  in->nStacks = catalogSeq.length;

  /* get timestamps of start, mid and end times of each stack */  
  /* set up vector containing mid times of stacks */    
  in->midTstack =  XLALCreateTimestampVector ( in->nStacks );
  
  /* set up vector containing start times of stacks */    
  in->startTstack =  XLALCreateTimestampVector ( in->nStacks );

  /* set up vector containing end times of stacks */    
  in->endTstack =  XLALCreateTimestampVector ( in->nStacks );
  
  /* now loop over stacks and get time stamps */
  for (k = 0; k < in->nStacks; k++) {
    
    if ( catalogSeq.data[k].length == 0 ) {
      /* something is wrong */
      ABORT ( status, HIERARCHICALSEARCH_EVAL, HIERARCHICALSEARCH_MSGEVAL );
    }
  
    /* start time of stack = time of first sft in stack */
    in->startTstack->data[k] = catalogSeq.data[k].data[0].header.epoch;
    
    /* end time of stack = time of last sft in stack */
    numSFT = catalogSeq.data[k].length;
    in->endTstack->data[k] = catalogSeq.data[k].data[numSFT - 1].header.epoch;

    /* reference time for Fstat has to be midpoint of segment */
    startTstackGPS = in->startTstack->data[k];
    endTstackGPS = in->endTstack->data[k];
    
    startTseg = XLALGPSGetREAL8( &startTstackGPS );
    endTseg = XLALGPSGetREAL8( &endTstackGPS );
    /*
    TRY ( LALGPStoFloat( status->statusPtr, &startTseg, &startTstackGPS ), status); 
    TRY ( LALGPStoFloat( status->statusPtr, &endTseg, &endTstackGPS ), status); 
    */
    midTseg = startTseg + ((endTseg - startTseg + timebase)*0.5);
    
    XLALGPSSetREAL8( &midTstackGPS, midTseg );
    /*
    TRY ( LALFloatToGPS( status->statusPtr, &midTstackGPS, &midTseg), status);
    */
    in->midTstack->data[k] = midTstackGPS;
    
  } /* loop over k */

  
  /* set reference time for pulsar parameters */
  /* first calculate the mid time of observation time span*/
  {
    REAL8 tStart8, tEnd8, tMid8;

    tStart8 = XLALGPSGetREAL8( &tStartGPS );
    tEnd8   = XLALGPSGetREAL8( &tEndGPS );
    tMid8 = 0.5 * (tStart8 + tEnd8);
    XLALGPSSetREAL8( &tMidGPS, tMid8 );
  }

  if ( in->refTime > 0)  {
    REAL8 refTime = in->refTime;
    XLALGPSSetREAL8(&refTimeGPS, refTime);
  }
  else {  /* set refTime to exact midtime of the total observation-time spanned */
    refTimeGPS = tMidGPS;
  }
  
  /* get frequency and fdot bands at start time of sfts by extrapolating from reftime */
  in->spinRange_refTime.refTime = refTimeGPS;
  TRY( LALExtrapolatePulsarSpinRange( status->statusPtr, &in->spinRange_startTime, tStartGPS, &in->spinRange_refTime), status); 
  TRY( LALExtrapolatePulsarSpinRange( status->statusPtr, &in->spinRange_endTime, tEndGPS, &in->spinRange_refTime), status); 
  TRY( LALExtrapolatePulsarSpinRange( status->statusPtr, &in->spinRange_midTime, tMidGPS, &in->spinRange_refTime), status); 


  /* set wings of sfts to be read */
  /* the wings must be enough for the Doppler shift and extra bins
     for the running median block size and Dterms for Fstat calculation.
     In addition, it must also include wings for the spindown correcting 
     for the reference time  */
  /* calculate Doppler wings at the highest frequency */
  startTime_freqLo = in->spinRange_startTime.fkdot[0]; /* lowest search freq at start time */
  startTime_freqHi = startTime_freqLo + in->spinRange_startTime.fkdotBand[0]; /* highest search freq. at start time*/
  endTime_freqLo = in->spinRange_endTime.fkdot[0];
  endTime_freqHi = endTime_freqLo + in->spinRange_endTime.fkdotBand[0];
  
  freqLo = HSMIN ( startTime_freqLo, endTime_freqLo );
  freqHi = HSMAX ( startTime_freqHi, endTime_freqHi );
  doppWings = freqHi * in->dopplerMax;    /* maximum Doppler wing -- probably larger than it has to be */
  extraBins = HSMAX ( in->blocksRngMed/2 + 1, in->Dterms );
  
  freqmin = freqLo - doppWings - extraBins * deltaFsft; 
  freqmax = freqHi + doppWings + extraBins * deltaFsft;
      
  /* ----- finally memory for segments of multi sfts ----- */
  stackMultiSFT->length = in->nStacks;
  stackMultiSFT->data = (MultiSFTVector **)LALCalloc(1, in->nStacks * sizeof(MultiSFTVector *));
  if ( stackMultiSFT->data == NULL ) {
    ABORT ( status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  }

  stackMultiDetStates->length = in->nStacks;
  stackMultiDetStates->data = (MultiDetectorStateSeries **)LALCalloc(1, in->nStacks * sizeof(MultiDetectorStateSeries *));
  if ( stackMultiDetStates->data == NULL ) {
    ABORT ( status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  }
  
  stackMultiNoiseWeights->length = in->nStacks;
  if ( in->SignalOnly )  {
    stackMultiNoiseWeights->data = (MultiNoiseWeights **)LALMalloc(in->nStacks * sizeof(MultiNoiseWeights *));
    if ( stackMultiNoiseWeights->data == NULL ) {
      ABORT ( status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
    }
  }
  else {
    stackMultiNoiseWeights->data = (MultiNoiseWeights **)LALCalloc(1, in->nStacks * sizeof(MultiNoiseWeights *));
    if ( stackMultiNoiseWeights->data == NULL ) {
      ABORT ( status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
    }
  }

 
  /* loop over segments and read sfts */  
  for (k = 0; k < in->nStacks; k++) {
    
    /* ----- load the multi-IFO SFT-vectors ----- */
    TRY( LALLoadMultiSFTs ( status->statusPtr, stackMultiSFT->data + k,  catalogSeq.data + k, 
                           freqmin, freqmax ), status);

    /* ----- obtain the (multi-IFO) 'detector-state series' for all SFTs ----- */
    TRY ( LALGetMultiDetectorStates ( status->statusPtr, stackMultiDetStates->data + k, 
                                     stackMultiSFT->data[k], in->edat ), status );
    
    /* ----- normalize sfts and compute noise weights ----- */
    if ( in->SignalOnly )  {
      stackMultiNoiseWeights->data[k] = NULL;
    }
    else {
      MultiPSDVector *psd = NULL;	
      TRY( LALNormalizeMultiSFTVect ( status->statusPtr, &psd, stackMultiSFT->data[k], 
				    in->blocksRngMed ), status );
      TRY( LALComputeMultiNoiseWeights  ( status->statusPtr, stackMultiNoiseWeights->data + k, 
					psd, in->blocksRngMed, 0 ), status );   
      TRY ( LALDestroyMultiPSDVector ( status->statusPtr, &psd ), status );
    } /* if ( in->SignalOnly )  */
    
 
  } /* loop over k */

  
  
  /* realloc if nStacks != in->nStacks */
  /*   if ( in->nStacks > nStacks ) { */
  
  /*     in->midTstack->length = nStacks; */
  /*     in->midTstack->data = (LIGOTimeGPS *)LALRealloc( in->midTstack->data, nStacks * sizeof(LIGOTimeGPS)); */
  
  /*     in->startTstack->length = nStacks; */
  /*     in->startTstack->data = (LIGOTimeGPS *)LALRealloc( in->startTstack->data, nStacks * sizeof(LIGOTimeGPS)); */
  
  /*     stackMultiSFT->length = nStacks; */
  /*     stackMultiSFT->data = (MultiSFTVector **)LALRealloc( stackMultiSFT->data, nStacks * sizeof(MultiSFTVector *)); */
  
  /*   }  */
  
  /* we don't need the original catalog anymore*/
  TRY( LALDestroySFTCatalog( status->statusPtr, &catalog ), status);  	
      
  /* free catalog sequence */
  for (k = 0; k < in->nStacks; k++)
    {
      if ( catalogSeq.data[k].length > 0 ) {
        LALFree(catalogSeq.data[k].data);
      } /* end if */
    } /* loop over stacks */
  LALFree( catalogSeq.data);  


#ifdef OUTPUT_TIMING
  /* need to count the total number of SFTs */
  nStacks = stackMultiSFT->length;
  nSFTs = 0;
  for ( k = 0; k < nStacks; k ++ )
    {
      UINT4 X;
      for ( X=0; X < stackMultiSFT->data[k]->length; X ++ )
        nSFTs += stackMultiSFT->data[k]->data[X]->length;
    } /* for k < stacks */
#endif

  DETATCHSTATUSPTR (status);
  RETURN(status);
  
} /* SetUpSFTs */




/** \brief Breaks up input sft catalog into specified number of stacks

    Loops over elements of the catalog, assigns a bin index and
    allocates memory to the output catalog sequence appropriately.  If
    there are long gaps in the data, then some of the catalogs in the
    output catalog sequence may be of zero length. 
*/
void SetUpStacks(LALStatus *status, 
		 SFTCatalogSequence  *out, /**< Output catalog of sfts -- one for each stack */
		 REAL8 tStack,             /**< Output duration of each stack */
		 SFTCatalog  *in,          /**< Input sft catalog to be broken up into stacks (ordered in increasing time)*/
		 UINT4 nStacksMax )        /**< User specified number of stacks */
{
  UINT4 j, stackCounter, length;
  REAL8 tStart, thisTime;
  REAL8 Tsft;

  INITSTATUS( status, "SetUpStacks", rcsid );
  ATTATCHSTATUSPTR (status);

  /* check input parameters */
  ASSERT ( in != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( in->length > 0, status, HIERARCHICALSEARCH_EVAL, HIERARCHICALSEARCH_MSGEVAL );
  ASSERT ( nStacksMax > 0, status, HIERARCHICALSEARCH_EVAL, HIERARCHICALSEARCH_MSGEVAL );
  ASSERT ( in != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( tStack > 0, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( out != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );

  /* set memory of output catalog sequence to maximum possible length */
  out->length = nStacksMax;
  out->data = (SFTCatalog *)LALCalloc( 1, nStacksMax * sizeof(SFTCatalog));
  if ( out->data == NULL ) {
    ABORT ( status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  }  

  
  Tsft = 1.0 / in->data[0].header.deltaF;

  /* get first sft timestamp */
  /* tStart will be start time of a given stack. 
     This initializes tStart to the first sft time stamp as this will 
     be the start time of the first stack */
  tStart = XLALGPSGetREAL8(&(in->data[0].header.epoch));

  /* loop over the sfts */
  stackCounter = 0; 
  for( j = 0; j < in->length; j++) 
    {
      /* thisTime is current sft timestamp */
      thisTime = XLALGPSGetREAL8(&(in->data[j].header.epoch));
    
      /* if sft lies in stack duration then add 
	 this sft to the stack. Otherwise move 
	 on to the next stack */
      if ( (thisTime - tStart + Tsft <= tStack) ) 
	{
	  out->data[stackCounter].length += 1;    
	  
	  length = out->data[stackCounter].length;
	  
	  /* realloc to increase length of catalog */    
	  out->data[stackCounter].data = (SFTDescriptor *)LALRealloc( out->data[stackCounter].data, length * sizeof(SFTDescriptor));
	  if ( out->data[stackCounter].data == NULL ) {
	    ABORT ( status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
	  }  

	  out->data[stackCounter].data[length - 1] = in->data[j];   
	}    
      else /* move onto the next stack */
	{ 
	  if ( stackCounter + 1 == nStacksMax )
	    break;
	  
	  stackCounter++;
	  
	  /* reset start time of stack */
    tStart = XLALGPSGetREAL8(&(in->data[j].header.epoch));
	  
	  /* realloc to increase length of catalog and copy data */    
	  out->data[stackCounter].length = 1;    /* first entry in new stack */
	  out->data[stackCounter].data = (SFTDescriptor *)LALRealloc( out->data[stackCounter].data, sizeof(SFTDescriptor));
	  if ( out->data[stackCounter].data == NULL ) {
	    ABORT ( status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
	  }  

	  out->data[stackCounter].data[0] = in->data[j];   
	} /* if new stack */
      
    } /* loop over sfts */

  /* realloc catalog sequence length to actual number of stacks */
  out->length = stackCounter + 1;
  out->data = (SFTCatalog *)LALRealloc( out->data, (stackCounter+1) * sizeof(SFTCatalog) );
  if ( out->data == NULL ) {
    ABORT ( status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  }  
  
  DETATCHSTATUSPTR (status);
  RETURN(status);

} /* SetUpStacks() */







/** Print some sft catalog info */
void PrintCatalogInfo( LALStatus  *status,
		       const SFTCatalog *catalog, 
		       FILE *fp)
{

  INT4 nSFT;
  LIGOTimeGPS start, end;
 
  INITSTATUS( status, "PrintCatalogInfo", rcsid );
  ATTATCHSTATUSPTR (status);

  ASSERT ( fp != NULL, status, HIERARCHICALSEARCH_EFILE, HIERARCHICALSEARCH_MSGEFILE );
  ASSERT ( catalog != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );

  nSFT = catalog->length;
  start = catalog->data[0].header.epoch;
  end = catalog->data[nSFT-1].header.epoch;

  fprintf(fp, "## Number of SFTs: %d\n", nSFT);
  fprintf(fp, "## First SFT timestamp: %d %d\n", start.gpsSeconds, start.gpsNanoSeconds);  
  fprintf(fp, "## Last SFT timestamp: %d %d\n", end.gpsSeconds, end.gpsNanoSeconds);    

  DETATCHSTATUSPTR (status);
  RETURN(status);

}



/** Print some stack info from sft catalog sequence*/
void PrintStackInfo( LALStatus  *status,
		     const SFTCatalogSequence *catalogSeq, 
		     FILE *fp)
{

  INT4 nStacks, k;
 
  INITSTATUS( status, "PrintStackInfo", rcsid );
  ATTATCHSTATUSPTR (status);

  ASSERT ( fp != NULL, status, HIERARCHICALSEARCH_EFILE, HIERARCHICALSEARCH_MSGEFILE );
  ASSERT ( catalogSeq != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( catalogSeq->length > 0, status, HIERARCHICALSEARCH_EVAL, HIERARCHICALSEARCH_MSGEVAL );
  ASSERT ( catalogSeq->data != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );

  nStacks = catalogSeq->length;
  fprintf(fp, "## Number of stacks: %d\n", nStacks);
  
  for ( k = 0; k < nStacks; k++) {
    fprintf(fp, "## Stack No. %d : \n", k+1);
    TRY ( PrintCatalogInfo( status->statusPtr, catalogSeq->data + k, fp), status);
  }

  fprintf(fp, "\n\n");

  DETATCHSTATUSPTR (status);
  RETURN(status);

}


/** Read checkpointing file 
    This does not (yet) check any consistency of 
    the existing results file */
void GetChkPointIndex( LALStatus *status,
		       INT4 *loopindex, 
		       const CHAR *fnameChkPoint)
{

  FILE  *fp=NULL;
  UINT4 tmpIndex;
  CHAR lastnewline='\0';
 
  INITSTATUS( status, "GetChkPointIndex", rcsid );
  ATTATCHSTATUSPTR (status);

  /* if something goes wrong later then lopindex will be 0 */
  *loopindex = 0;

  /* try to open checkpoint file */
  if (!(fp = fopen(fnameChkPoint, "rb"))) 
    {
      if ( lalDebugLevel )
	fprintf (stdout, "Checkpoint-file '%s' not found.\n", fnameChkPoint);

      DETATCHSTATUSPTR (status);
      RETURN(status);
    }

  /* if we are here then checkpoint file has been found */
  if ( lalDebugLevel )
    fprintf ( stdout, "Found checkpoint-file '%s' \n", fnameChkPoint);

  /* check the checkpointfile -- it should just have one integer 
     and a DONE on the next line */
  if ( ( 2 != fscanf (fp, "%" LAL_UINT4_FORMAT "\nDONE%c", &tmpIndex, &lastnewline) ) || ( lastnewline!='\n' ) ) 
    {
      fprintf ( stdout, "Failed to read checkpoint index from '%s'!\n", fnameChkPoint);
      fclose(fp);

      DETATCHSTATUSPTR (status);
      RETURN(status);
    }
  
  /* everything seems ok -- set loop index */
  *loopindex = tmpIndex;

  fclose( fp );  
  
  DETATCHSTATUSPTR (status);
  RETURN(status);

}



/** Get SemiCoh candidates toplist */
void GetSemiCohToplist(LALStatus *status,
                       toplist_t *list,
                       FineGrid *in,
                       UsefulStageVariables *usefulparams)
{

  UINT4 k, Nstacks;
  INT4 debug;
  GCTtopOutputEntry line;

  INITSTATUS( status, "GetSemiCohToplist", rcsid );
  ATTATCHSTATUSPTR (status);

  ASSERT ( list != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( in != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( usefulparams != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
 
  Nstacks = usefulparams->nStacks;
  
  /* go through candidates and insert into toplist if necessary */
  for ( k = 0; k < in->length; k++) {

    line.Freq = in->list[k].F;
    line.Alpha = in->Alpha;
    line.Delta = in->Delta;
    line.F1dot = in->list[k].F1dot;
    line.nc = in->list[k].nc;
    line.sumTwoF = in->list[k].sumTwoF / Nstacks; /* save the average 2F value */

    debug = INSERT_INTO_GCTFSTAT_TOPLIST( list, line);

  }
  
  DETATCHSTATUSPTR (status);
  RETURN(status); 

} /* GetSemiCohToplist() */



/** Translate fine-grid spin parameters to specified reference time */
void TranslateFineGridSpins(LALStatus *status,
                       UsefulStageVariables *usefulparams,
                       FineGrid *in)
{
  
  UINT4 k;
  PulsarSpins fkdot;
  
  INITSTATUS( status, "TranslateFineGridSpins", rcsid );
  ATTATCHSTATUSPTR (status);
  
  INIT_MEM(fkdot);
  
  ASSERT ( usefulparams != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( in != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  
  /* go through candidates and translate spins to reference time */
  for ( k = 0; k < in->length; k++) {
    
    fkdot[0] = in->list[k].F;
    fkdot[1] = in->list[k].F1dot;
    
    /* propagate fkdot to reference-time  */
    TRY ( LALExtrapolatePulsarSpins (status->statusPtr, 
                                     fkdot, usefulparams->spinRange_refTime.refTime, fkdot, in->refTime), 
         status );
    
    /* assign translated frequency value */
    in->list[k].F = fkdot[0];
    
  }
  
  DETATCHSTATUSPTR (status);
  RETURN(status); 
  
} /* TranslateFineGridSpins() */







/** Calculate Earth orbital position, velocity and acceleration
    at midpoint of each segment */
void GetSegsPosVelAccEarthOrb( LALStatus *status, 
		       REAL8VectorSequence **posSeg,                     
		       REAL8VectorSequence **velSeg,
		       REAL8VectorSequence **accSeg,
		       UsefulStageVariables *usefulparams)
{

  UINT4 k, nStacks;
  LIGOTimeGPSVector *tsMid;
  vect3Dlist_t *pvaUR = NULL;
    
  INITSTATUS( status, "GetSegsPosVelAccEarthOrb", rcsid );
  ATTATCHSTATUSPTR (status);

  ASSERT ( usefulparams != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( usefulparams->nStacks > 0, status, HIERARCHICALSEARCH_EVAL, HIERARCHICALSEARCH_MSGEVAL );
  ASSERT ( usefulparams->midTstack != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( posSeg != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( velSeg != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );
  ASSERT ( accSeg != NULL, status, HIERARCHICALSEARCH_ENULL, HIERARCHICALSEARCH_MSGENULL );

  /* local copies */
  nStacks = usefulparams->nStacks;
  tsMid =  usefulparams->midTstack;
  
  /* get pos,vel,acc at midpoint of each segment*/
  for (k = 0; k < nStacks; k++)
    {
      /* initialize velocities and positions */
      posSeg[0]->data[3*k]   = 0.0;
      posSeg[0]->data[3*k+1] = 0.0;
      posSeg[0]->data[3*k+2] = 0.0;
    
      velSeg[0]->data[3*k]   = 0.0;
      velSeg[0]->data[3*k+1] = 0.0;
      velSeg[0]->data[3*k+2] = 0.0;
    
      accSeg[0]->data[3*k]   = 0.0;
      accSeg[0]->data[3*k+1] = 0.0;
      accSeg[0]->data[3*k+2] = 0.0;
         
      /* get Earth's orbital pos vel acc  */
      if ( (pvaUR = XLALComputeOrbitalDerivatives(3, &tsMid->data[k], usefulparams->edat) ) == NULL ) {
        LogPrintf(LOG_CRITICAL,"GetSegsPosVelAccEarthOrb(): XLALComputeOrbitalDerivatives() failed.\n");
        ABORT ( status, HIERARCHICALSEARCH_ESFT, HIERARCHICALSEARCH_MSGESFT );
      }
      
      posSeg[0]->data[3*k]   = pvaUR->data[0][0];
      posSeg[0]->data[3*k+1] = pvaUR->data[0][1];
      posSeg[0]->data[3*k+2] = pvaUR->data[0][2];
      
      velSeg[0]->data[3*k]   = pvaUR->data[1][0];
      velSeg[0]->data[3*k+1] = pvaUR->data[1][1];
      velSeg[0]->data[3*k+2] = pvaUR->data[1][2];
      
      accSeg[0]->data[3*k]   = pvaUR->data[2][0];
      accSeg[0]->data[3*k+1] = pvaUR->data[2][1];
      accSeg[0]->data[3*k+2] = pvaUR->data[2][2];

      XLALDestroyVect3Dlist ( pvaUR );
      
    } /* loop over segment -- end pos vel acc of Earth's orbital motion */

  
  DETATCHSTATUSPTR (status);
  RETURN(status);
  
} /* GetSegsPosVelAccEarthOrb() */





/** Calculate the U1 index for a given point in parameter space */
void ComputeU1idx( LALStatus *status, 
                  const REAL8 *f_event, 
                  const REAL8 *f1dot_event, 
                  const REAL8 *A1, 
                  const REAL8 *B1, 
                  const REAL8 *U1start, 
                  const REAL8 *U1winInv,
                  INT4 *U1idx)
{
  
  REAL8 freqL, f1dotL, A1L, B1L, U1startL, U1winInvL;
  
  INITSTATUS( status, "ComputeU1idx", rcsid );
  ATTATCHSTATUSPTR (status);
  
  /* Local copies */
  freqL = *f_event;
  f1dotL = *f1dot_event;
  A1L = *A1;
  B1L = *B1;
  U1startL = *U1start;
  U1winInvL = *U1winInv;
  
  /* compute the index of global-correlation coordinate U1 */
  *U1idx = (INT4) ((((freqL * A1L + f1dotL * B1L) - U1startL) * U1winInvL) + 0.5);

  DETATCHSTATUSPTR (status);
  RETURN(status);
  
} /* ComputeU1idx */


/** Calculate the U2 index for a given point in parameter space */
void ComputeU2idx( LALStatus *status, 
                  const REAL8 *f_event, 
                  const REAL8 *f1dot_event, 
                  const REAL8 *A2, 
                  const REAL8 *B2, 
                  const REAL8 *U2start, 
                  const REAL8 *U2winInv,
                  INT4 *U2idx)
{
  
  REAL8 freqL, f1dotL, A2L, B2L, U2startL, U2winInvL;
  
  INITSTATUS( status, "ComputeU2idx", rcsid );
  ATTATCHSTATUSPTR (status);
  
  /* Local copies */
  freqL = *f_event;
  f1dotL = *f1dot_event;
  A2L = *A2;
  B2L = *B2;
  U2startL = *U2start;
  U2winInvL = *U2winInv;
  
  /* compute the index of global-correlation coordinate U2 */ 
  *U2idx = (INT4) ((((f1dotL + freqL * A2L + 2.0 * f1dotL * B2L) - U2startL) * U2winInvL) + 0.5);
  
  DETATCHSTATUSPTR (status);
  RETURN(status);
  
} /* ComputeU2idx */


 



/** Comparison function for sorting the coarse grid in u1 and u2 */
int compareCoarseGridUindex(const void *a,const void *b) {
  CoarseGridPoint a1, b1;
  a1 = *((const CoarseGridPoint *)a);
  b1 = *((const CoarseGridPoint *)b);
  
  if( a1.Uindex < b1.Uindex )
    return(-1);
  else if( a1.Uindex > b1.Uindex)
    return(1);
  else
    return(0);
}

/** Comparison function for sorting the fine grid in u1 and u2*/
int compareFineGridUindex(const void *a,const void *b) {
  FineGridPoint a1, b1;
  a1 = *((const FineGridPoint *)a);
  b1 = *((const FineGridPoint *)b);
  
  if( a1.Uindex < b1.Uindex )
    return(-1);
  else if( a1.Uindex > b1.Uindex)
    return(1);
  else       
    return(0);
}

/** Comparison function for sorting the fine grid in number count */
int compareFineGridNC(const void *a,const void *b) {
  FineGridPoint a1, b1;
  a1 = *((const FineGridPoint *)a);
  b1 = *((const FineGridPoint *)b);
  
  if( a1.nc < b1.nc )
    return(1);
  else if( a1.nc > b1.nc)
    return(-1);
  else     
    return(0);
}

/** Comparison function for sorting the fine grid in summed 2F */
int compareFineGridsumTwoF(const void *a,const void *b) {
  FineGridPoint a1, b1;
  a1 = *((const FineGridPoint *)a);
  b1 = *((const FineGridPoint *)b);
  
  if( a1.sumTwoF < b1.sumTwoF )
    return(1);
  else if( a1.sumTwoF > b1.sumTwoF)
    return(-1);
  else     
    return(0);
}

/** Simply output version information to stdout */
void OutputVersion ( void )
{
  printf ( "%s\n", lalGitID );
  printf ( "%s\n", lalappsGitID );
  
  return;
  
} /* OutputVersion() */

