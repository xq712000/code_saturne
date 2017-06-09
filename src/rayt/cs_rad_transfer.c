/*============================================================================
 * Radiation solver operations.
 *============================================================================*/

/* This file is part of Code_Saturne, a general-purpose CFD tool.

  Copyright (C) 1998-2017 EDF S.A.

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
  Street, Fifth Floor, Boston, MA 02110-1301, USA. */

/*----------------------------------------------------------------------------*/

#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <float.h>

#if defined(HAVE_MPI)
#include <mpi.h>
#endif

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include "bft_error.h"
#include "bft_mem.h"
#include "bft_printf.h"

#include "cs_boundary_zone.h"
#include "cs_log.h"
#include "cs_mesh.h"
#include "cs_mesh_quantities.h"
#include "cs_parall.h"
#include "cs_parameters.h"
#include "cs_sles.h"
#include "cs_sles_it.h"
#include "cs_timer.h"

#include "cs_prototypes.h"

/*----------------------------------------------------------------------------
 *  Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_rad_transfer.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*=============================================================================
 * Additional Doxygen documentation
 *============================================================================*/

/*! \file  cs_rad_transfer.c
           Radiation solver operations.
*/

/*----------------------------------------------------------------------------*/

/*!

  \struct cs_rad_transfer_params_t

  \brief Structure containing the radiation module parameters.

  \var  cs_rad_transfer_params_t::iirayo
        Activate (\f$>0\f$) or deactivate (=0) the radiation module.
        The different values correspond to the following modelling methods:
        - 1: discrete ordinates method
        (DOM standard option for radiation in semi-transparent media)
        - 2: "P-1" method\n
        Warning: the P-1 method allows faster computations, but it
        may only be applied to media with uniform large optical thickness,
        such as some cases of pulverised coal combustion.
 \var  cs_rad_transfer_params_t::nrphas
        Phase which radiates (bulk by default, but may be coal class or fuel
        droplets phase).
  \var  cs_rad_transfer_params_t::iimpar
        Verbosity level in the listing concerning the calculation of
        the wall temperatures:
        - 0: no display
        - 1: standard
        - 2: complete
  \var  cs_rad_transfer_params_t::iimlum
        Verbosity level in the listing concerning the calculation of
        the radiative transfer equation:
        - 0: no display
        - 1: standard
        - 2: complete
  \var  cs_rad_transfer_params_t::imodak
        When gas or coal combustion is activated, \ref imodak indicates whether
        the absorption coefficient shall be calculated "automatically" (=1)
        or read from the data file (=0).
  \var  cs_rad_transfer_params_t::imoadf
        ADF model:
        - 0 no ADF model
        - 1 ADF model with 8 intervals of wave length
        - 2 ADF model with 50 intervals of wave length
  \var  cs_rad_transfer_params_t::iwrp1t
        P1 model transparency warnings counter.
  \var  cs_rad_transfer_params_t::imfsck
        FSCK model:
        - 0 no FSCK model
        - 1 FSCK model activated
  \var  cs_rad_transfer_params_t::xnp1mx
        For the P-1 model, percentage of cells for which we allow the optical
        thickness to exceed unity, although this should be avoided.
        (more precisely, where \f$ KL \f$ is lower than 1, where \f$ K \f$ is
        the absorption coefficient of the medium and \f$ L \f$ is a
        characteristic length of the domain).
  \var  cs_rad_transfer_params_t::idiver
        Indicates the method used to calculate the radiative source term:
        - 0: semi-analytic calculation (compulsory with transparent media)
        - 1: conservative calculation
        - 2: semi-analytic calculation corrected in order to be globally
        conservative
  \var  cs_rad_transfer_params_t::i_quadrature
        Index of the quadrature and number of directions for a single octant.\n
        Sn quadrature (n(n+2) directions)
        - 1: S4 (24 directions)
        - 2: S6 (48 directions)
        - 3: S8 (80 directions)\n
        Tn quadrature (8n^2 directions)
        - 4: T2 (32 directions)
        - 5: T4 (128 directions)
        - 6: Tn (8*ndirec^2 directions)
  \var  cs_rad_transfer_params_t::ndirec
        Number of directions for the angular discretisation of the radiation
        propagation with the DOM model (\ref iirayo = 1)\n
        No other possible value, because of the way the directions are calculated.\n
        The calculation with 32 directions may break the symmetry of physically
        axi-symmetric cases (but the cost in CPU time is much lower than with
        128 directions).\n
        Useful if and only if the radiation module is activated with the DOM method.
  \var  cs_rad_transfer_params_t::ndirs
        For the Tn quadrature, \ref ndirec squared
  \var  cs_rad_transfer_params_t::sxyz
        Directions of angular values of the quadrature sx, sy, sz.
  \var  cs_rad_transfer_params_t::angsol
        Weight of the solid angle.
  \var  cs_rad_transfer_params_t::restart
        Indicates whether the radiation variables should be initialized or
        read from a restart file.
  \var  cs_rad_transfer_params_t::nfreqr
        Period of the radiation module.
        The radiation module is called every \ref nfreqr time steps (more precisely,
        every time \ref optcal::ntcabs "ntcabs" is a multiple of \ref nfreqr).
        Also, in order to have proper initialization of the variables, whatever
        the value of \ref nfreqr, the radiation module is called at
        the first time step of a calculation (restart or not).
  \var  cs_rad_transfer_params_t::nwsgg
        Spectral radiation models (ADF and FSCK).\n
        Number of ETRs to solve.
  \var  cs_rad_transfer_params_t::wq
        Weights of the Gaussian quadrature
  \var  cs_rad_transfer_params_t::itpimp
        Wall face with imposed temperature.
  \var  cs_rad_transfer_params_t::ipgrno
        For a grey or black wall face, calculation of the
        temperature by means of a flux balance.
  \var  cs_rad_transfer_params_t::iprefl
        For a reflecting wall face, calculation of the
        temperature by means of a flux balance. This is
        fixed at 2000 in radiat and cannot be modified.
  \var  cs_rad_transfer_params_t::ifgrno
        Grey or black wall face to which a conduction flux is imposed.
  \var  cs_rad_transfer_params_t::ifrefl
        Reflecting wall face to which a conduction flux is imposed,
        which is equivalent to impose this flux directly to the fluid.
  \var  cs_rad_transfer_params_t::itpt1d
        Calculation of the temperature with the 1D wall thermal module,
        which solves a heat equation.
*/

/*----------------------------------------------------------------------------*/

/*! \cond DOXYGEN_SHOULD_SKIP_THIS */

/*=============================================================================
 * Local Macro Definitions
 *============================================================================*/

/*=============================================================================
 * Local type definitions
 *============================================================================*/

/*============================================================================
 * Global variables
 *============================================================================*/

cs_rad_transfer_params_t _rt_params = {.iirayo = 0,
                                       .nrphas = 0,
                                       .iimpar = 0,
                                       .iimlum = 0,
                                       .imodak = 0,
                                       .imoadf = 0,
                                       .iwrp1t = 0,
                                       .imfsck = 0,
                                       .xnp1mx = 0.,
                                       .idiver = 0,
                                       .i_quadrature = 0,
                                       .ndirec = 0,
                                       .ndirs = 0,
                                       .sxyz = NULL,
                                       .angsol = NULL,
                                       .restart = 0,
                                       .nfreqr = 0,
                                       .nwsgg = 0,
                                       .wq = NULL,
                                       .itpimp = 1,
                                       .ipgrno = 21,
                                       .iprefl = 22,
                                       .ifgrno = 31,
                                       .ifrefl = 32,
                                       .itpt1d = 4 };

cs_rad_transfer_params_t *cs_glob_rad_transfer_params = &_rt_params;

/*============================================================================
 * Prototypes for functions intended for use only by Fortran wrappers.
 * (descriptions follow, with function bodies).
 *============================================================================*/

void
cs_rad_transfer_get_pointers(int  **p_iirayo,
                             int  **p_nfreqr);

/*============================================================================
 * Fortran wrapper function definitions
 *============================================================================*/

void
cs_rad_transfer_get_pointers(int  **p_iirayo,
                             int  **p_nfreqr)
{
  *p_iirayo = &_rt_params.iirayo;
  *p_nfreqr = &_rt_params.nfreqr;
}

/*============================================================================
 * Private function definitions
 *============================================================================*/

/*! (DOXYGEN_SHOULD_SKIP_THIS) \endcond */

/*============================================================================
 * Public function definitions for Fortran API
 *============================================================================*/

/*=============================================================================
 * Public function definitions
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief Finalize radiative transfer module.
 */
/*----------------------------------------------------------------------------*/

void
cs_rad_transfer_finalize(void)
{
  BFT_FREE(_rt_params.sxyz);
  BFT_FREE(_rt_params.angsol);
  BFT_FREE(_rt_params.wq);
}

/*----------------------------------------------------------------------------*/

END_C_DECLS