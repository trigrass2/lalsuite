/*----------------------------------------------------------------------- 
 * 
 * File Name: ZZVectorMultiply.c
 * 
 * Author: Creighton, J. D. E.
 * 
 * Revision: $Id$
 * 
 *----------------------------------------------------------------------- 
 * 
 * NAME 
 * ZZVectorMultiply
 * 
 * SYNOPSIS 
 *
 *
 * DESCRIPTION 
 * Multiply two double-precision complex vectors.
 * 
 * DIAGNOSTICS 
 * Null pointer, invalid input size, size mismatch.
 *
 * CALLS
 * 
 * NOTES
 * 
 *----------------------------------------------------------------------- 
 * 
 * REVISION HISTORY 
 * 
 * $Log$
 * Revision 1.3  2000/07/23 01:16:13  jolien
 * LAL-prefix in front of functions and Status; debuglevel is now LALDebugLevel.
 *
 * Revision 1.2  2000/04/17 23:31:14  jolien
 * Removed redundant include protection.
 * Changed INITSTATUS() to take extra argument (function name).
 *
 * Revision 1.1  2000/03/12 23:21:10  jolien
 * Initial entry.
 *
 * 
 *-----------------------------------------------------------------------
 */

#include <math.h>
#include "LALStdlib.h"
#include "VectorOps.h"

NRCSID(ZZVECTORMULTIPLYC,"$Id$");

void
LALZZVectorMultiply (
    LALStatus                *status,
    COMPLEX16Vector       *out,
    const COMPLEX16Vector *in1,
    const COMPLEX16Vector *in2
    )
{
  COMPLEX16 *a;
  COMPLEX16 *b;
  COMPLEX16 *c;
  INT4       n;

  INITSTATUS (status, "LALZZVectorMultiply", ZZVECTORMULTIPLYC);

  ASSERT (out, status, VECTOROPS_ENULL, VECTOROPS_MSGENULL);
  ASSERT (in1, status, VECTOROPS_ENULL, VECTOROPS_MSGENULL);

  ASSERT (in2, status, VECTOROPS_ENULL, VECTOROPS_MSGENULL);

  ASSERT (out->data, status, VECTOROPS_ENULL, VECTOROPS_MSGENULL);
  ASSERT (in1->data, status, VECTOROPS_ENULL, VECTOROPS_MSGENULL);
  ASSERT (in2->data, status, VECTOROPS_ENULL, VECTOROPS_MSGENULL);

  ASSERT (out->length > 0, status, VECTOROPS_ESIZE, VECTOROPS_MSGESIZE);
  ASSERT (in1->length == out->length, status,
          VECTOROPS_ESZMM, VECTOROPS_MSGESZMM);
  ASSERT (in2->length == out->length, status,
          VECTOROPS_ESZMM, VECTOROPS_MSGESZMM);

  a = in1->data;
  b = in2->data;
  c = out->data;
  n = out->length;

  while (n-- > 0)
  {
    REAL8 ar = a->re;
    REAL8 ai = a->im;
    REAL8 br = b->re;
    REAL8 bi = b->im;

    c->re = ar*br - ai*bi;
    c->im = ar*bi + ai*br;

    ++a;
    ++b;
    ++c;
  }

  RETURN (status);
}

