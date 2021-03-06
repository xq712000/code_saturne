/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2019 EDF S.A.

  This program is free software; you can redistribute it and/or modify it under
  the terms of the GNU General Public License as published by the Free Software
  Foundation; either version 2 of the License, or (at your option) any later
  version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
  details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 51 Franklin
  Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <stdio.h>

/*----------------------------------------------------------------------------
 * Local headers
 *----------------------------------------------------------------------------*/

#include "runmilieu.h"

/*----------------------------------------------------------------------------
 *  Header for the current file
 *----------------------------------------------------------------------------*/

#include "donnees.h"

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 *  Global variables
 *============================================================================*/

int  nbpdtm = 0;
int  nbssit = 0;

int  isyncp = 0;
int  ntchr = 0;

double  dtref = 0.0;
double  ttinit = 0.0;

double  epsilo = 0.0;

/*============================================================================
 * Public function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------
 * Fonction inter_cs_ast_set_nbpdtm
 *
 * Get time step defined in coupling case XML description
 *----------------------------------------------------------------------------*/

void
inter_cs_ast_set_nbpdtm(long i)
{
  printf("\n i = %d \n", (int)i);
  nbpdtm = i;
  printf("\n nbpdtm = %d \n", nbpdtm);
}

/*----------------------------------------------------------------------------
 * Fonction inter_cs_ast_set_nbssit
 *
 * Get number of iterations of implicit coupling defined in coupling
 * case XML description
 *----------------------------------------------------------------------------*/

void
inter_cs_ast_set_nbssit(long i)
{
  nbssit = i;
}

/*----------------------------------------------------------------------------
 * Fonction inter_cs_ast_set_isyncp
 *
 * Get "isyncp" defined in coupling case XML description
 *----------------------------------------------------------------------------*/

void
inter_cs_ast_set_isyncp(long i)
{
  isyncp = i;
}

/*----------------------------------------------------------------------------
 * Fonction inter_cs_ast_set_ntchr
 *
 * Get "ntchr" defined in coupling case XML description
 *----------------------------------------------------------------------------*/

void
inter_cs_ast_set_ntchr(long i)
{
  ntchr = i;
}

/*----------------------------------------------------------------------------
 * Fonction inter_cs_ast_set_dtref
 *
 * Get "dtref" defined in coupling case XML description
 *----------------------------------------------------------------------------*/

void
inter_cs_ast_set_dtref(double dt)
{
  dtref = dt;
}

/*----------------------------------------------------------------------------
 * Fonction inter_cs_ast_set_ttinit
 *
 * Get "ttinit" defined in coupling case XML description
 *----------------------------------------------------------------------------*/

void
inter_cs_ast_set_ttinit(double tt)
{
  ttinit = tt;
}

/*----------------------------------------------------------------------------
 * Fonction inter_cs_ast_set_epsilo
 *
 * Get "epsilo" defined in coupling case XML description
 *----------------------------------------------------------------------------*/

void
inter_cs_ast_set_epsilo(double eps)
{
  epsilo = eps;
}

/*----------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif
