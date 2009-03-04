/*
 *  Copyright (C) 2007 Badri Krishnan
 *  Copyright (C) 2008 Christine Chung, Badri Krishnan and John Whelan
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
 */

/**
 * \author Christine Chung, Badri Krishnan, John Whelan
 * \date 2008
 * \file 
 * \ingroup pulsar
 * \brief Header-file for LAL routines for CW cross-correlation searches
 *
 * $Id$
 *
 */
 
/*
 *   Protection against double inclusion (include-loop protection)
 *     Note the naming convention!
 */

#ifndef _PULSARCROSSCORR_V0_H
#define _PULSARCROSSCORR_V0_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <glob.h>
#include <time.h>
#include <errno.h> 

#include <lal/AVFactories.h>
#include <lal/Date.h>
#include <lal/DetectorSite.h>
#include <lal/LALDatatypes.h>
#include <lal/LALHough.h>
#include <lal/RngMedBias.h>
#include <lal/LALRunningMedian.h>
#include <lal/Velocity.h>
#include <lal/Statistics.h>
#include <lal/ComputeFstat.h>
#include <lal/UserInput.h>
#include <lal/SFTfileIO.h>
#include <lal/NormalizeSFTRngMed.h>
#include <lal/LALInitBarycenter.h>
#include <lal/SFTClean.h>
#include <gsl/gsl_cdf.h>
#include <lal/FrequencySeries.h>
#include <lal/Sequence.h>


/******************************************************
 *   Protection against C++ name mangling
 */

#ifdef  __cplusplus
extern "C" {
#endif


/******************************************************
 *  Assignment of Id string using NRCSID()
 */

NRCSID (PULSARCROSSCORR_V0_H, "$Id$");

/******************************************************
 *  Error codes and messages.
 */
 
#define PULSARCROSSCORR_V0_ENULL 1
#define PULSARCROSSCORR_V0_ENONULL 2

#define PULSARCROSSCORR_V0_MSGENULL "Null pointer"
#define PULSARCROSSCORR_V0_MSGENONULL "Non-null pointer"

/* ******************************************************************
 *  Structure, enum, union, etc., typdefs.
 */

  /** struct holding info about skypoints */
  typedef struct tagSkyPatchesInfo_v0{
    UINT4 numSkyPatches;
    REAL8 *alpha;
    REAL8 *delta;
    REAL8 *alphaSize;
    REAL8 *deltaSize;
  } SkyPatchesInfo_v0;

  typedef struct tagSFTPairparams_v0{
    REAL8 lag;
  } SFTPairParams_v0;

  /* define structs to hold combinations of F's and A's */
  typedef struct tagCrossCorrAmps_v0 {
	REAL8 Aplussq;
	REAL8 Acrosssq;
	REAL8 AplusAcross;
	} CrossCorrAmps_v0;

  typedef struct tagCrossCorrBeamFn_v0{
	REAL8 Fplus_or_a;
	REAL8 Fcross_or_b;
	} CrossCorrBeamFn_v0;



/*
 *  Functions Declarations (i.e., prototypes).
 */

void LALCombineAllSFTs_v0 ( LALStatus *status,
			 SFTVector **outsfts,
			 MultiSFTVector *multiSFTs,
			 REAL8 length);

void LALCreateSFTPairsIndicesFrom2SFTvectors_v0(LALStatus          *status,
					     INT4VectorSequence **out,
					     SFTVector          *in,
					     SFTPairParams_v0      *par,
					     INT4 		detChoice);

void LALCorrelateSingleSFTPair_v0(LALStatus                *status,
			       COMPLEX16                *out,
			       COMPLEX8FrequencySeries  *sft1,
			       COMPLEX8FrequencySeries  *sft2,
			       REAL8FrequencySeries     *psd1,
			       REAL8FrequencySeries     *psd2,
			       REAL8                    *freq1,
			       REAL8                    *freq2);

void LALGetSignalFrequencyInSFT_v0(LALStatus                *status,
				REAL8                    *out,
				COMPLEX8FrequencySeries  *sft1,
				PulsarDopplerParams      *dopp,
				REAL8Vector              *vel,
				LIGOTimeGPS	         *firstTimeStamp);

void LALGetSignalPhaseInSFT_v0(LALStatus               *status,
			    REAL8                   *out,
			    COMPLEX8FrequencySeries *sft1,
			    PulsarDopplerParams     *dopp,
			    REAL8Vector             *pos);

void LALCalculateSigmaAlphaSq_v0(LALStatus            *status,
			      REAL8                *out,
			      REAL8                freq1,
			      REAL8                freq2,
			      REAL8FrequencySeries *psd1,
			      REAL8FrequencySeries *psd2);

void LALCalculateAveUalpha_v0(LALStatus *status,
			COMPLEX16 *out,
			REAL8     *phiI,
			REAL8     *phiJ,
			CrossCorrBeamFn_v0 beamfnsI,
			CrossCorrBeamFn_v0 beamfnsJ,
			REAL8     *sigmasq);

void LALCalculateUalpha_v0(LALStatus *status,
			COMPLEX16 *out,
			CrossCorrAmps_v0 amplitudes,
			REAL8     *phiI,
			REAL8     *phiJ,
			CrossCorrBeamFn_v0 beamfnsI,
			CrossCorrBeamFn_v0 beamfnsJ,
			REAL8     *sigmasq);

void LALCalculateCrossCorrPower_v0(LALStatus       *status,
				REAL8	        *out,
				COMPLEX16Vector *yalpha,
				COMPLEX16Vector *ualpha);

void LALNormaliseCrossCorrPower_v0(LALStatus        *status,
				REAL8		 *out,
				COMPLEX16Vector  *ualpha,
				REAL8Vector      *sigmaAlphasq);

/* ****************************************************** */

#ifdef  __cplusplus
}                /* Close C++ protection */
#endif


#endif     /* Close double-include protection _PULSARCROSSCORR_H */