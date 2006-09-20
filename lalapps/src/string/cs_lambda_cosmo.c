/*********************************************************************************/
/*           Cosmological functions for cosmic string burst computation          */
/*                                                                               */
/*                  Jolien Creighton, Irit Maor, Xavier Siemens                  */
/*                                                                               */
/*                         UWM/Caltech - September 2006                          */
/*********************************************************************************/
#include <math.h>
#include <gsl/gsl_integration.h>
#include "cs_lambda_cosmo.h"

/*  "$Id$" "$Name$"; */

static double cs_lambda_hubble( double one_plus_z )
{
	const double Omega_m = LAMBDA_OMEGA_M;
	const double Omega_r = LAMBDA_OMEGA_R;
	const double Omega_L = 1.0 - LAMBDA_OMEGA_M - LAMBDA_OMEGA_R;
	double one_plus_z_3 = one_plus_z * one_plus_z * one_plus_z;
	double one_plus_z_4 = one_plus_z * one_plus_z_3;
	double ans;
	/* Eq. (A2) of [SCMRMCR] */
	ans = sqrt( Omega_m * one_plus_z_3 + Omega_r * one_plus_z_4 + Omega_L );
	return ans;
}

static double cs_lambda_phit_integrand( double y, void * p )
{
	double one_plus_z;
	double z;
	double ans;

	p = NULL;
	z = 1.0 / y;
	one_plus_z = 1.0 + z;
	/* Integrand of Eq. (A4) of [SCMRMCR] */
	ans  = 1.0 / ( one_plus_z * cs_lambda_hubble( one_plus_z ) );
	ans *= z * z; /* from change of variables */
	return ans;
}

static double cs_lambda_phiA_integrand( double z, void * p )
{
	double ans;

	p = NULL;
	/* Integrand of Eq. (A6) of [SCMRMCR] */
	ans = 1.0 / cs_lambda_hubble( 1.0 + z );
	return ans;
}

#define WORKSZ 100000
#define EPS 1e-7
cs_cosmo_functions_t cs_cosmo_functions_alloc( double zmin, double dlnz, size_t n )
{
	cs_cosmo_functions_t cosmofns;
	gsl_integration_workspace * w = gsl_integration_workspace_alloc(WORKSZ);
	gsl_function F1, F2;
	size_t i;

	cosmofns.zmin = zmin;
	cosmofns.dlnz = dlnz;
	cosmofns.n    = n;
	cosmofns.z    = calloc( n, sizeof( *cosmofns.z ) );
	cosmofns.phit = calloc( n, sizeof( *cosmofns.phit ) );
	cosmofns.phiA = calloc( n, sizeof( *cosmofns.phiA ) );
	cosmofns.phiV = calloc( n, sizeof( *cosmofns.phiV ) );

	F1.params = F2.params = NULL;
	F1.function = &cs_lambda_phiA_integrand;
	F2.function = &cs_lambda_phit_integrand;

	for ( i = 0; i < n; ++i )
	{
		double one_plus_z;
		double one_plus_z_3;
		double err;
		double h;

		cosmofns.z[i] = zmin * exp( i * dlnz );

		gsl_integration_qag (&F1, 0, cosmofns.z[i], 0, EPS, WORKSZ, 1, w, cosmofns.phiA+i, &err );
		gsl_integration_qag (&F2, 0, 1.0/cosmofns.z[i], 0, EPS, WORKSZ, 1, w, cosmofns.phit+i, &err );
		/* Eq. (A8) of [SCMRMCR] */
		one_plus_z = 1.0 + cosmofns.z[i];
		one_plus_z_3 = one_plus_z * one_plus_z * one_plus_z;
		h = cs_lambda_hubble( one_plus_z );
		cosmofns.phiV[i] = 4.0 * M_PI * cosmofns.phiA[i] * cosmofns.phiA[i] / ( one_plus_z_3 * h );
	}

	gsl_integration_workspace_free( w );
	return cosmofns;
}

cs_cosmo_functions_t cs_cosmo_functions( double *z, size_t n )
{
	cs_cosmo_functions_t cosmofns;
	gsl_integration_workspace * w = gsl_integration_workspace_alloc(WORKSZ);
	gsl_function F1, F2;
	size_t i;

	cosmofns.zmin = z[0];
	cosmofns.dlnz = 0;
	cosmofns.n    = n;
	cosmofns.z    = calloc( n, sizeof( *cosmofns.z ) );
	cosmofns.phit = calloc( n, sizeof( *cosmofns.phit ) );
	cosmofns.phiA = calloc( n, sizeof( *cosmofns.phiA ) );
	cosmofns.phiV = calloc( n, sizeof( *cosmofns.phiV ) );

	F1.params = F2.params = NULL;
	F1.function = &cs_lambda_phiA_integrand;
	F2.function = &cs_lambda_phit_integrand;

	for ( i = 0; i < n; ++i )
	{
		double one_plus_z;
		double one_plus_z_3;
		double err;
		double h;

		cosmofns.z[i] = z[i];

		gsl_integration_qag (&F1, 0, cosmofns.z[i], 0, EPS, WORKSZ, 1, w, cosmofns.phiA+i, &err );
		gsl_integration_qag (&F2, 0, 1.0/cosmofns.z[i], 0, EPS, WORKSZ, 1, w, cosmofns.phit+i, &err );
		/* Eq. (A8) of [SCMRMCR] */
		one_plus_z = 1.0 + cosmofns.z[i];
		one_plus_z_3 = one_plus_z * one_plus_z * one_plus_z;
		h = cs_lambda_hubble( one_plus_z );
		cosmofns.phiV[i] = 4.0 * M_PI * cosmofns.phiA[i] * cosmofns.phiA[i] / ( one_plus_z_3 * h );
	}

	gsl_integration_workspace_free( w );
	return cosmofns;
}
