/*============================================================================
 * Routines to handle common features for building algebraic system in CDO
 * schemes
 *============================================================================*/

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

#include "cs_defs.h"

/*----------------------------------------------------------------------------
 * Standard C library headers
 *----------------------------------------------------------------------------*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>

/*----------------------------------------------------------------------------
 *  Local headers
 *----------------------------------------------------------------------------*/

#include <bft_mem.h>

#include "cs_boundary_zone.h"
#include "cs_cdo_local.h"
#include "cs_log.h"
#include "cs_math.h"
#include "cs_parall.h"
#include "cs_xdef_eval.h"

#if defined(DEBUG) && !defined(NDEBUG)
#include "cs_dbg.h"
#endif

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*----------------------------------------------------------------------------
 * Header for the current file
 *----------------------------------------------------------------------------*/

#include "cs_equation_common.h"

/*----------------------------------------------------------------------------*/

BEGIN_C_DECLS

/*============================================================================
 * Type definitions and macros
 *============================================================================*/

#define CS_EQUATION_COMMON_DBG               0 /* Debug level */
#define CS_EQUATION_COMMON_PROFILE_ASSEMBLY  0 /* > 0 --> Activate profiling */

/*============================================================================
 * Local private variables
 *============================================================================*/

/* Temporary buffers useful during the building of all algebraic systems */
static size_t  cs_equation_common_work_buffer_size = 0;
static cs_real_t  *cs_equation_common_work_buffer = NULL;

/* Store the matrix structure and its assembler structures for each family
   of space discretizations */
static cs_matrix_assembler_t  **cs_equation_common_ma = NULL;
static cs_matrix_structure_t  **cs_equation_common_ms = NULL;
static cs_equation_assembly_buf_t  **cs_assembly_buffers = NULL;

/* Pointer to shared structures (owned by a cs_domain_t structure) */
static const cs_cdo_quantities_t  *cs_shared_quant;
static const cs_cdo_connect_t  *cs_shared_connect;
static const cs_time_step_t  *cs_shared_time_step;

/* Monitoring/Profiling of the assembly process for CDO equations */
#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0
static unsigned int  n_assembly_value_calls = 0;
static cs_timer_counter_t  tcas; /* matrix structure assembly processing */
static cs_timer_counter_t  tcav; /* matrix values assembly processing */
#endif

/*! \cond DOXYGEN_SHOULD_SKIP_THIS */

/*============================================================================
 * Private function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Allocate and define a cs_matrix_assembler_t structure
 *
 * \param[in]  n_elts     number of elements
 * \param[in]  n_dofbyx   number of DoFs by element
 * \param[in]  x2x        pointer to a cs_adjacency_t structure
 * \param[in]  rs         pointer to a range set or NULL if sequential
 *
 * \return a pointer to a new allocated cs_matrix_assembler_t structure
 */
/*----------------------------------------------------------------------------*/

static cs_matrix_assembler_t *
_build_matrix_assembler(cs_lnum_t                n_elts,
                        int                      n_dofbyx,
                        const cs_adjacency_t    *x2x,
                        const cs_range_set_t    *rs)
{
  cs_gnum_t  *grows = NULL, *gcols = NULL;

  /* The second paramter is set to "true" meaning that the diagonal is stored
     separately --> MSR storage */
  cs_matrix_assembler_t  *ma = cs_matrix_assembler_create(rs->l_range, true);

  /* First loop to count max size of the buffer */
  cs_lnum_t  max_size = 0;
  for (cs_lnum_t id = 0; id < n_elts; id++)
    max_size = CS_MAX(max_size, x2x->idx[id+1] - x2x->idx[id]);

  /* We increment max_size to take into account the diagonal entry */
  int  buf_size = n_dofbyx * n_dofbyx * (max_size + 1);
  BFT_MALLOC(grows, buf_size, cs_gnum_t);
  BFT_MALLOC(gcols, buf_size, cs_gnum_t);

  if (n_dofbyx == 1)  { /* Simplified version */

    for (cs_lnum_t row_id = 0; row_id < n_elts; row_id++) {

      const cs_gnum_t  grow_id = rs->g_id[row_id];
      const cs_lnum_t  start = x2x->idx[row_id];
      const cs_lnum_t  end = x2x->idx[row_id+1];

      /* Diagonal term is excluded in this connectivity. Add it "manually" */
      grows[0] = grow_id, gcols[0] = grow_id;

      /* Extra diagonal couples */
      for (cs_lnum_t j = start, i = 1; j < end; j++, i++) {
        grows[i] = grow_id;
        gcols[i] = rs->g_id[x2x->ids[j]];
      }

      cs_matrix_assembler_add_g_ids(ma, end - start + 1, grows, gcols);

    } /* Loop on entities */

  }
  else {

    for (cs_lnum_t row_id = 0; row_id < n_elts; row_id++) {

      const cs_lnum_t  start = x2x->idx[row_id];
      const cs_lnum_t  end = x2x->idx[row_id+1];
      const int  n_entries = (end - start + 1) * n_dofbyx * n_dofbyx;
      const cs_gnum_t  *grow_ids = rs->g_id + row_id*n_dofbyx;

      int shift = 0;

      /* Diagonal term is excluded in this connectivity. Add it "manually" */
      for (int dof_i = 0; dof_i < n_dofbyx; dof_i++) {
        const cs_gnum_t  grow_id = grow_ids[dof_i];
        for (int dof_j = 0; dof_j < n_dofbyx; dof_j++) {
          grows[shift] = grow_id;
          gcols[shift] = grow_ids[dof_j];
          shift++;
        }
      }

      /* Extra diagonal couples */
      for (cs_lnum_t j = start; j < end; j++) {

        const cs_lnum_t  col_id = x2x->ids[j];
        const cs_gnum_t  *gcol_ids = rs->g_id + col_id*n_dofbyx;

        for (int dof_i = 0; dof_i < n_dofbyx; dof_i++) {
          const cs_gnum_t  grow_id = grow_ids[dof_i];
          for (int dof_j = 0; dof_j < n_dofbyx; dof_j++) {
            grows[shift] = grow_id;
            gcols[shift] = gcol_ids[dof_j];
            shift++;
          }
        }

      } /* Loop on number of DoFs by entity */

      assert(shift == n_entries);
      cs_matrix_assembler_add_g_ids(ma, n_entries, grows, gcols);

    } /* Loop on entities */

  }

  /* Now compute structure */
  cs_matrix_assembler_compute(ma);

  /* Free temporary buffers */
  BFT_FREE(grows);
  BFT_FREE(gcols);

  return ma;
}

/*! (DOXYGEN_SHOULD_SKIP_THIS) \endcond */

/*============================================================================
 * Public function prototypes
 *============================================================================*/

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Define a \ref cs_matrix_assembler_values_t structure
 *
 * \param[in, out] matrix          pointer to matrix structure
 * \param[in]      omp_choice      choice of the OpenMP strategy
 * \param[in]      stride          stride to apply to each entity
 *
 * \return  a pointer to a cs_matrix_assembler_values_t
 */
/*----------------------------------------------------------------------------*/

cs_matrix_assembler_values_t *
cs_equation_get_mav(cs_matrix_t                        *matrix,
                    cs_param_omp_assembly_strategy_t    omp_choice,
                    int                                 stride)
{
  cs_matrix_assembler_values_t  *mav = NULL;

  switch (stride) {

  case 1:
    if (cs_glob_n_threads < 2)  /* Single thread */
      mav
        = cs_matrix_assembler_values_initx(matrix, NULL, NULL,
                                           true,  /* MSR */
                                   cs_matrix_msr_assembler_values_init,
                                   cs_matrix_msr_assembler_values_add_1_single,
                                           NULL,
                                           NULL, NULL); /* Optional pointers */
    else if (omp_choice == CS_PARAM_OMP_ASSEMBLY_ATOMIC)
      mav
        = cs_matrix_assembler_values_initx(matrix, NULL, NULL,
                                           true,  /* MSR */
                                   cs_matrix_msr_assembler_values_init,
                                   cs_matrix_msr_assembler_values_add_1_atomic,
                                           NULL,
                                           NULL, NULL); /* Optional pointers */
    else if (omp_choice == CS_PARAM_OMP_ASSEMBLY_CRITICAL)
      mav
        = cs_matrix_assembler_values_initx(matrix, NULL, NULL,
                                           true,  /* MSR */
                                   cs_matrix_msr_assembler_values_init,
                                   cs_matrix_msr_assembler_values_add_1_critic,
                                           NULL,
                                           NULL, NULL); /* Optional pointers */
    else
      bft_error(__FILE__, __LINE__, 0, "%s: Invalid OpenMP choice", __func__);
    break;

  default:
    bft_error(__FILE__, __LINE__, 0, "%s: Case not handled.", __func__);
    break;

  }

  return mav;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Retrieve the pointer to a requested \ref cs_matrix_structure_t
 *         structure
 *
 * \param[in]  flag_id       id in the array of matrix structures
 *
 * \return  a pointer to a cs_matrix_structure_t
 */
/*----------------------------------------------------------------------------*/

cs_matrix_structure_t *
cs_equation_get_matrix_structure(int  flag)
{
  if (cs_equation_common_ms == NULL || flag < 0)
    return NULL;

  if (flag < CS_CDO_CONNECT_N_CASES)
    return cs_equation_common_ms[flag];
  else
    return NULL;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Allocate a pointer to a buffer of size at least the 2*n_cells for
 *         managing temporary usage of memory when dealing with equations
 *         Call specific structure allocation related to a numerical scheme
 *         according to the scheme flag
 *         The size of the temporary buffer can be bigger according to the
 *         numerical settings
 *         Set also shared pointers from the main domain members
 *
 * \param[in]  connect      pointer to a cs_cdo_connect_t structure
 * \param[in]  quant        pointer to additional mesh quantities struct.
 * \param[in]  time_step    pointer to a time step structure
 * \param[in]  vb_flag      metadata for Vb schemes
 * \param[in]  vcb_flag     metadata for V+C schemes
 * \param[in]  fb_flag      metadata for Fb schemes
 * \param[in]  hho_flag     metadata for HHO schemes
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_allocate_structures(const cs_cdo_connect_t       *connect,
                                const cs_cdo_quantities_t    *quant,
                                const cs_time_step_t         *time_step,
                                cs_flag_t                     vb_flag,
                                cs_flag_t                     vcb_flag,
                                cs_flag_t                     fb_flag,
                                cs_flag_t                     hho_flag)
{
  assert(connect != NULL && quant != NULL); /* Sanity check */

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
  cs_timer_t t0, t1;

  CS_TIMER_COUNTER_INIT(tcas); /* matrix structure assembly process */
  CS_TIMER_COUNTER_INIT(tcav); /* matrix value assembly process */
#endif

  /* Two types of mat. ass. are considered:
   *  - The one related to matrix based on vertices
   *  - The one related to matrix based on faces
   */
  BFT_MALLOC(cs_equation_common_ma,
             CS_CDO_CONNECT_N_CASES, cs_matrix_assembler_t *);
  for (int i = 0; i < CS_CDO_CONNECT_N_CASES; i++)
    cs_equation_common_ma[i] = NULL;

  BFT_MALLOC(cs_equation_common_ms,
             CS_CDO_CONNECT_N_CASES, cs_matrix_structure_t *);
  for (int i = 0; i < CS_CDO_CONNECT_N_CASES; i++)
    cs_equation_common_ms[i] = NULL;

  /* Allocate cell-wise and face-wise view of a mesh */
  cs_cdo_local_initialize(connect);

  const cs_lnum_t  n_cells = connect->n_cells;
  const cs_lnum_t  n_faces = connect->n_faces[0];
  const cs_lnum_t  n_vertices = connect->n_vertices;

  /* Allocate shared buffer and initialize shared structures */
  size_t  cwb_size = n_cells; /* initial cell-wise buffer size */
  int  loc_assembler_size = 0;
  int  assembler_dof_size = 0;

  const int  _vb_system_max_size = connect->n_max_vbyc*connect->n_max_vbyc;
  const int  _fb_system_max_size = connect->n_max_fbyc*connect->n_max_fbyc;

  /* Allocate and initialize matrix assembler and matrix structures */
  if (vb_flag > 0 || vcb_flag > 0) {

    if (vb_flag & CS_FLAG_SCHEME_SCALAR || vcb_flag & CS_FLAG_SCHEME_SCALAR) {

      const cs_range_set_t  *rs = connect->range_sets[CS_CDO_CONNECT_VTX_SCAL];

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t0 = cs_timer_time();
#endif

      cs_matrix_assembler_t  *ma = _build_matrix_assembler(n_vertices,
                                                           1,
                                                           connect->v2v,
                                                           rs);
      cs_matrix_structure_t  *ms =
        cs_matrix_structure_create_from_assembler(CS_MATRIX_MSR, ma);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t1 = cs_timer_time();
      cs_timer_counter_add_diff(&tcas, &t0, &t1);
#endif

      cs_equation_common_ma[CS_CDO_CONNECT_VTX_SCAL] = ma;
      cs_equation_common_ms[CS_CDO_CONNECT_VTX_SCAL] = ms;

      if (vb_flag & CS_FLAG_SCHEME_SCALAR) {

        cwb_size = CS_MAX(cwb_size, (size_t)n_vertices);
        loc_assembler_size = CS_MAX(loc_assembler_size, _vb_system_max_size);
        assembler_dof_size = CS_MAX(assembler_dof_size, connect->n_max_vbyc);

      }

      if (vcb_flag & CS_FLAG_SCHEME_SCALAR) {

        cwb_size = CS_MAX(cwb_size, (size_t)(n_vertices + n_cells));
        loc_assembler_size = CS_MAX(loc_assembler_size, _vb_system_max_size);
        assembler_dof_size = CS_MAX(assembler_dof_size, connect->n_max_vbyc);

      }

    } /* scalar-valued equations */

    if (vb_flag & CS_FLAG_SCHEME_VECTOR || vcb_flag & CS_FLAG_SCHEME_VECTOR) {

      const cs_range_set_t  *rs = connect->range_sets[CS_CDO_CONNECT_VTX_VECT];

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t0 = cs_timer_time();
#endif

      cs_matrix_assembler_t  *ma = _build_matrix_assembler(n_vertices,
                                                           3,
                                                           connect->v2v,
                                                           rs);
      cs_matrix_structure_t  *ms =
        cs_matrix_structure_create_from_assembler(CS_MATRIX_MSR, ma);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t1 = cs_timer_time();
      cs_timer_counter_add_diff(&tcas, &t0, &t1);
#endif

      cs_equation_common_ma[CS_CDO_CONNECT_VTX_VECT] = ma;
      cs_equation_common_ms[CS_CDO_CONNECT_VTX_VECT] = ms;

      cwb_size *= 3; /* 3*n_cells by default */
      if (vb_flag & CS_FLAG_SCHEME_VECTOR) {

        cwb_size = CS_MAX(cwb_size, (size_t)3*n_vertices);
        loc_assembler_size = CS_MAX(loc_assembler_size, 9*_vb_system_max_size);
        assembler_dof_size = CS_MAX(assembler_dof_size, 3*connect->n_max_vbyc);


      }

      if (vcb_flag & CS_FLAG_SCHEME_VECTOR) {

        cwb_size = CS_MAX(cwb_size, (size_t)3*(n_vertices + n_cells));
        loc_assembler_size = CS_MAX(loc_assembler_size, 9*_vb_system_max_size);
        assembler_dof_size = CS_MAX(assembler_dof_size, 3*connect->n_max_vbyc);

      }

    } /* vector-valued equations */

  } /* Vertex-based schemes and related ones */

  if (fb_flag > 0 || hho_flag > 0) {

    cs_matrix_structure_t  *ms0 = NULL, *ms1 = NULL, *ms2 = NULL;
    cs_matrix_assembler_t  *ma0 = NULL, *ma1 = NULL, *ma2 = NULL;

    if (cs_flag_test(fb_flag, CS_FLAG_SCHEME_POLY0 | CS_FLAG_SCHEME_SCALAR) ||
        cs_flag_test(hho_flag, CS_FLAG_SCHEME_POLY0 | CS_FLAG_SCHEME_SCALAR)) {

      const cs_range_set_t  *rs = connect->range_sets[CS_CDO_CONNECT_FACE_SP0];

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t0 = cs_timer_time();
#endif

      ma0 = _build_matrix_assembler(n_faces, 1, connect->f2f, rs);
      ms0 = cs_matrix_structure_create_from_assembler(CS_MATRIX_MSR, ma0);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t1 = cs_timer_time();
      cs_timer_counter_add_diff(&tcas, &t0, &t1);
#endif

      cs_equation_common_ma[CS_CDO_CONNECT_FACE_SP0] = ma0;
      cs_equation_common_ms[CS_CDO_CONNECT_FACE_SP0] = ms0;

      if (fb_flag & CS_FLAG_SCHEME_SCALAR) {

        assert(n_faces > n_cells);
        cwb_size = CS_MAX(cwb_size, (size_t)n_faces);
        loc_assembler_size = CS_MAX(loc_assembler_size, _fb_system_max_size);
        assembler_dof_size = CS_MAX(assembler_dof_size, connect->n_max_fbyc);

      }

      if (hho_flag & CS_FLAG_SCHEME_SCALAR)
        cwb_size = CS_MAX(cwb_size, (size_t)n_faces);

    } /* Scalar-valued CDO-Fb or HHO-P0 */

    if (cs_flag_test(fb_flag, CS_FLAG_SCHEME_POLY0 | CS_FLAG_SCHEME_VECTOR) ||
        cs_flag_test(hho_flag, CS_FLAG_SCHEME_POLY1 | CS_FLAG_SCHEME_SCALAR) ||
        cs_flag_test(hho_flag, CS_FLAG_SCHEME_POLY0 | CS_FLAG_SCHEME_VECTOR)) {

      const cs_range_set_t  *rs = connect->range_sets[CS_CDO_CONNECT_FACE_SP1];

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t0 = cs_timer_time();
#endif

      ma1 = _build_matrix_assembler(n_faces,
                                    CS_N_FACE_DOFS_1ST,
                                    connect->f2f,
                                    rs);
      ms1 = cs_matrix_structure_create_from_assembler(CS_MATRIX_MSR, ma1);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t1 = cs_timer_time();
      cs_timer_counter_add_diff(&tcas, &t0, &t1);
#endif

      assert((CS_CDO_CONNECT_FACE_SP1 == CS_CDO_CONNECT_FACE_VP0) &&
             (CS_CDO_CONNECT_FACE_SP1 == CS_CDO_CONNECT_FACE_VHP0));

      cs_equation_common_ma[CS_CDO_CONNECT_FACE_SP1] = ma1;
      cs_equation_common_ms[CS_CDO_CONNECT_FACE_SP1] = ms1;

      cwb_size = CS_MAX(cwb_size, (size_t)CS_N_FACE_DOFS_1ST * n_faces);
      loc_assembler_size = CS_MAX(loc_assembler_size, 9*_fb_system_max_size);
      assembler_dof_size = CS_MAX(assembler_dof_size, 3*connect->n_max_fbyc);

    } /* Vector CDO-Fb or HHO-P1 or vector HHO-P0 */

    if (cs_flag_test(hho_flag,
                     CS_FLAG_SCHEME_POLY2 | CS_FLAG_SCHEME_SCALAR)) {

      const cs_range_set_t  *rs = connect->range_sets[CS_CDO_CONNECT_FACE_SP2];

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t0 = cs_timer_time();
#endif

      ma2 = _build_matrix_assembler(n_faces,
                                    CS_N_FACE_DOFS_2ND,
                                    connect->f2f,
                                    rs);
      ms2 = cs_matrix_structure_create_from_assembler(CS_MATRIX_MSR, ma2);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
      t1 = cs_timer_time();
      cs_timer_counter_add_diff(&tcas, &t0, &t1);
#endif

      cs_equation_common_ma[CS_CDO_CONNECT_FACE_SP2] = ma2;
      cs_equation_common_ms[CS_CDO_CONNECT_FACE_SP2] = ms2;

      cwb_size = CS_MAX(cwb_size, (size_t)CS_N_FACE_DOFS_2ND * n_faces);
      /* 36 = 6 * 6 */
      loc_assembler_size = CS_MAX(loc_assembler_size, 36*_fb_system_max_size);
      assembler_dof_size = CS_MAX(assembler_dof_size, 6*connect->n_max_fbyc);

    }

    /* For vector equations and HHO */
    if (cs_flag_test(hho_flag, CS_FLAG_SCHEME_VECTOR | CS_FLAG_SCHEME_POLY1) ||
        cs_flag_test(hho_flag, CS_FLAG_SCHEME_VECTOR | CS_FLAG_SCHEME_POLY2)) {

      if  (hho_flag & CS_FLAG_SCHEME_POLY1) {

        const cs_range_set_t  *rs
          = connect->range_sets[CS_CDO_CONNECT_FACE_VHP1];

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
        t0 = cs_timer_time();
#endif

        ma1 = _build_matrix_assembler(n_faces,
                                      3*CS_N_FACE_DOFS_1ST,
                                      connect->f2f,
                                      rs);
        ms1 = cs_matrix_structure_create_from_assembler(CS_MATRIX_MSR, ma1);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
        t1 = cs_timer_time();
        cs_timer_counter_add_diff(&tcas, &t0, &t1);
#endif

        cs_equation_common_ma[CS_CDO_CONNECT_FACE_VHP1] = ma1;
        cs_equation_common_ms[CS_CDO_CONNECT_FACE_VHP1] = ms1;

        cwb_size = CS_MAX(cwb_size, (size_t)3*CS_N_FACE_DOFS_1ST*n_faces);
        /* 81 = 9 * 9 (where 9 = 3*3) */
        loc_assembler_size = CS_MAX(loc_assembler_size, 81*_fb_system_max_size);
        assembler_dof_size = CS_MAX(assembler_dof_size, 9*connect->n_max_fbyc);

      }
      else if  (hho_flag & CS_FLAG_SCHEME_POLY2) {

        const cs_range_set_t  *rs
          = connect->range_sets[CS_CDO_CONNECT_FACE_VHP2];

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
        t0 = cs_timer_time();
#endif

        ma2 = _build_matrix_assembler(n_faces,
                                      3*CS_N_FACE_DOFS_2ND,
                                      connect->f2f,
                                      rs);
        ms2 = cs_matrix_structure_create_from_assembler(CS_MATRIX_MSR, ma2);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
        t1 = cs_timer_time();
        cs_timer_counter_add_diff(&tcas, &t0, &t1);
#endif

        cs_equation_common_ma[CS_CDO_CONNECT_FACE_VHP2] = ma2;
        cs_equation_common_ms[CS_CDO_CONNECT_FACE_VHP2] = ms2;

        cwb_size = CS_MAX(cwb_size, (size_t)3*CS_N_FACE_DOFS_2ND*n_faces);
        /* 324 = 18 * 18 (where 18 = 3*6) */
        loc_assembler_size = CS_MAX(loc_assembler_size,
                                    324*_fb_system_max_size);
        assembler_dof_size = CS_MAX(assembler_dof_size, 18*connect->n_max_fbyc);

      }

    }

  } /* Face-based schemes (CDO or HHO) */

  /* Assign static const pointers: shared pointers with a cs_domain_t */
  cs_shared_quant = quant;
  cs_shared_connect = connect;
  cs_shared_time_step = time_step;

  /* Common buffer for temporary usage */
  cs_equation_common_work_buffer_size = cwb_size;
  BFT_MALLOC(cs_equation_common_work_buffer, cwb_size, double);

  /* Common buffers for assembly usage */
  BFT_MALLOC(cs_assembly_buffers, cs_glob_n_threads,
             cs_equation_assembly_buf_t *);
  for (int i = 0; i < cs_glob_n_threads; i++)
    cs_assembly_buffers[i] = NULL;

#if defined(HAVE_OPENMP) /* Determine the default number of OpenMP threads */
#pragma omp parallel
  {
    /* Each thread allocate its part. This yields a better memory affinity */
    int  t_id = omp_get_thread_num();

    BFT_MALLOC(cs_assembly_buffers[t_id], 1, cs_equation_assembly_buf_t);

    cs_equation_assembly_buf_t  *assb = cs_assembly_buffers[t_id];

    assb->n_x_dofs = 1;           /* Default setting */
    BFT_MALLOC(assb->dof_gids, assembler_dof_size, cs_gnum_t);

    assb->buffer_size = loc_assembler_size;
    BFT_MALLOC(assb->row_gids, loc_assembler_size, cs_gnum_t);
    BFT_MALLOC(assb->col_gids, loc_assembler_size, cs_gnum_t);
    BFT_MALLOC(assb->values, loc_assembler_size, cs_real_t);
  }
#else
  BFT_MALLOC(cs_assembly_buffers[0], 1, cs_equation_assembly_buf_t);

  cs_equation_assembly_buf_t  *assb = cs_assembly_buffers[0];

  assb->n_x_dofs = 1;           /* Default setting */
  BFT_MALLOC(assb->dof_gids, assembler_dof_size, cs_gnum_t);

  assb->buffer_size = loc_assembler_size;
  BFT_MALLOC(assb->row_gids, loc_assembler_size, cs_gnum_t);
  BFT_MALLOC(assb->col_gids, loc_assembler_size, cs_gnum_t);
  BFT_MALLOC(assb->values, loc_assembler_size, cs_real_t);
#endif
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Allocate a pointer to a buffer of size at least the 2*n_cells for
 *         managing temporary usage of memory when dealing with equations
 *         Call specific structure allocation related to a numerical scheme
 *         according to the scheme flag
 *         The size of the temporary buffer can be bigger according to the
 *         numerical settings
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_free_structures(void)
{
  /* Free cell-wise and face-wise view of a mesh */
  cs_cdo_local_finalize();

  /* Free common buffer */
  BFT_FREE(cs_equation_common_work_buffer);

  /* Free common assembly buffers */
#if defined(HAVE_OPENMP) /* Determine the default number of OpenMP threads */
#pragma omp parallel
  {
    int  t_id = omp_get_thread_num();
    cs_equation_assembly_buf_t  *assb = cs_assembly_buffers[t_id];

    BFT_FREE(assb->row_gids);
    BFT_FREE(assb->col_gids);
    BFT_FREE(assb->values);
    BFT_FREE(assb);
  }
#else
  cs_equation_assembly_buf_t  *assb = cs_assembly_buffers[0];

  BFT_FREE(assb->row_gids);
  BFT_FREE(assb->col_gids);
  BFT_FREE(assb->values);
  BFT_FREE(assb);
#endif

  BFT_FREE(cs_assembly_buffers);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
  cs_timer_t  t0 = cs_timer_time();
#endif

  /* Free matrix structures */
  for (int i = 0; i < CS_CDO_CONNECT_N_CASES; i++)
    cs_matrix_structure_destroy(&(cs_equation_common_ms[i]));
  BFT_FREE(cs_equation_common_ms);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
  cs_timer_t  t1 = cs_timer_time();
  cs_timer_counter_add_diff(&tcas, &t0, &t1);
#endif

  /* Free matrix assemblers */
  for (int i = 0; i < CS_CDO_CONNECT_N_CASES; i++)
    cs_matrix_assembler_destroy(&(cs_equation_common_ma[i]));
  BFT_FREE(cs_equation_common_ma);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
  cs_timer_t t2 = cs_timer_time();
  cs_timer_counter_add_diff(&tcav, &t1, &t2);

  cs_log_printf(CS_LOG_PERFORMANCE, " %-32s %12s %12s\n",
                " ", "Assembly.Struct", "Assembly.Values (Time/n_calls)");
  cs_log_printf(CS_LOG_PERFORMANCE, " %-35s %10.3f %10.3f seconds %u calls\n",
                "<CDO/CommonEq> Runtime",
                tcas.wall_nsec*1e-9, tcav.wall_nsec*1e-9/cs_glob_n_threads,
                n_assembly_value_calls);
#endif
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Allocate a new structure to handle the building of algebraic system
 *         related to a cs_equation_t structure
 *
 * \param[in] eqp       pointer to a cs_equation_param_t structure
 * \param[in] mesh      pointer to a cs_mesh_t structure
 *
 * \return a pointer to a new allocated cs_equation_builder_t structure
 */
/*----------------------------------------------------------------------------*/

cs_equation_builder_t *
cs_equation_init_builder(const cs_equation_param_t   *eqp,
                         const cs_mesh_t             *mesh)
{
  cs_equation_builder_t  *eqb = NULL;

  BFT_MALLOC(eqb, 1, cs_equation_builder_t);

  /* Initialize flags used to knows what kind of cell quantities to build */
  eqb->msh_flag = 0;
  eqb->bd_msh_flag = 0;
  eqb->st_msh_flag = 0;
  if (eqp->dim > 1)
    eqb->sys_flag = CS_FLAG_SYS_VECTOR;
  else
    eqb->sys_flag = 0;

  /* Handle properties */
  eqb->diff_pty_uniform = true;
  if (cs_equation_param_has_diffusion(eqp))
    eqb->diff_pty_uniform = cs_property_is_uniform(eqp->diffusion_property);

  eqb->time_pty_uniform = true;
  if (cs_equation_param_has_time(eqp))
    eqb->time_pty_uniform = cs_property_is_uniform(eqp->time_property);

  if (eqp->n_reaction_terms > CS_CDO_N_MAX_REACTIONS)
    bft_error(__FILE__, __LINE__, 0,
              " Number of reaction terms for an equation is too high.\n"
              " Modify your settings aor contact the developpement team.");

  for (int i = 0; i < eqp->n_reaction_terms; i++)
    eqb->reac_pty_uniform[i]
      = cs_property_is_uniform(eqp->reaction_properties[i]);

  /* Handle source terms */
  eqb->source_mask = NULL;
  if (cs_equation_param_has_sourceterm(eqp)) {

    /* Default intialization */
    eqb->st_msh_flag = cs_source_term_init(eqp->space_scheme,
                                           eqp->n_source_terms,
                       (cs_xdef_t *const *)eqp->source_terms,
                                           eqb->compute_source,
                                           &(eqb->sys_flag),
                                           &(eqb->source_mask));

  } /* There is at least one source term */

  /* Set members and structures related to the management of the BCs
     Translate user-defined information about BC into a structure well-suited
     for computation. We make the distinction between homogeneous and
     non-homogeneous BCs.  */
  eqb->face_bc = cs_cdo_bc_face_define(eqp->default_bc,
                                       true, /* Steady BC up to now */
                                       eqp->dim,
                                       eqp->n_bc_defs,
                                       eqp->bc_defs,
                                       mesh->n_b_faces);

  /* Monitoring */
  CS_TIMER_COUNTER_INIT(eqb->tcb); /* build system */
  CS_TIMER_COUNTER_INIT(eqb->tcd); /* build diffusion terms */
  CS_TIMER_COUNTER_INIT(eqb->tca); /* build advection terms */
  CS_TIMER_COUNTER_INIT(eqb->tcr); /* build reaction terms */
  CS_TIMER_COUNTER_INIT(eqb->tcs); /* build source terms */
  CS_TIMER_COUNTER_INIT(eqb->tce); /* extra operations */

  return eqb;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Free a cs_equation_builder_t structure
 *
 * \param[in, out]  p_builder  pointer of pointer to the cs_equation_builder_t
 *                             structure to free
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_free_builder(cs_equation_builder_t  **p_builder)
{
  if (p_builder == NULL)
    return;
  if (*p_builder == NULL)
    return;

  cs_equation_builder_t  *eqb = *p_builder;

  if (eqb->source_mask != NULL)
    BFT_FREE(eqb->source_mask);

  /* Free BC structure */
  eqb->face_bc = cs_cdo_bc_free(eqb->face_bc);

  BFT_FREE(eqb);

  *p_builder = NULL;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief Prepare a linear system and synchronize buffers to handle parallelism.
 *        Transfer a mesh-based description of arrays x0 and rhs into an
 *        algebraic description for the linear system in x and b.
 *
 * \param[in]      stride   stride to apply to the range set operations
 * \param[in]      x_size   size of the vector unknows (scatter view)
 * \param[in]      matrix   pointer to a cs_matrix_t structure
 * \param[in]      rset     pointer to a range set structure
 * \param[in, out] x        array of unknows (in: initial guess)
 * \param[in, out] b        right-hand side
 *
 * \returns the number of non-zeros in the matrix
 */
/*----------------------------------------------------------------------------*/

cs_gnum_t
cs_equation_prepare_system(int                   stride,
                           cs_lnum_t             x_size,
                           const cs_matrix_t    *matrix,
                           cs_range_set_t       *rset,
                           cs_real_t            *x,
                           cs_real_t            *b)
{
  const cs_lnum_t  n_scatter_elts = x_size; /* size of x and rhs */
  const cs_lnum_t  n_gather_elts = cs_matrix_get_n_rows(matrix);

  /* Sanity checks */
  assert(n_gather_elts <= n_scatter_elts);

#if defined(DEBUG) && !defined(NDEBUG) && CS_EQUATION_COMMON_DBG > 0
  cs_log_printf(CS_LOG_DEFAULT,
                " n_gather_elts:    %d\n"
                " n_scatter_elts:   %d\n"
                " n_matrix_rows:    %d\n"
                " n_matrix_columns: %d\n",
                n_gather_elts, n_scatter_elts, cs_matrix_get_n_rows(matrix),
                cs_matrix_get_n_columns(matrix));
#endif

  if (cs_glob_n_ranks > 1) { /* Parallel mode */
                             /* ============= */

    /* x and b should be changed to have a "gathered" view through the range set
       operation.  Their size is equal to n_sles_gather_elts <=
       n_sles_scatter_elts */

    /* Compact numbering to fit the algebraic decomposition */
    cs_range_set_gather(rset,
                        CS_REAL_TYPE, /* type */
                        stride,       /* stride */
                        x,            /* in: size = n_sles_scatter_elts */
                        x);           /* out: size = n_sles_gather_elts */

    /* The right-hand side stems from a cellwise building on this rank.
       Other contributions from distant ranks may contribute to an element
       owned by the local rank */
    cs_interface_set_sum(rset->ifs,
                         n_scatter_elts, stride, false, CS_REAL_TYPE,
                         b);

    cs_range_set_gather(rset,
                        CS_REAL_TYPE,/* type */
                        stride,      /* stride */
                        b,           /* in: size = n_sles_scatter_elts */
                        b);          /* out: size = n_sles_gather_elts */
  }

  /* Output information related to the linear system */
  const cs_lnum_t  *row_index, *col_id;
  const cs_real_t  *d_val, *x_val;

  cs_matrix_get_msr_arrays(matrix, &row_index, &col_id, &d_val, &x_val);

#if defined(DEBUG) && !defined(NDEBUG) && CS_EQUATION_COMMON_DBG > 2
  cs_dbg_dump_linear_system("Dump linear system",
                            n_gather_elts, CS_EQUATION_COMMON_DBG,
                            x, b,
                            row_index, col_id, x_val, d_val);
#endif

  cs_gnum_t  nnz = row_index[n_gather_elts];
  cs_parall_counter(&nnz, 1);

  return nnz;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief   Print a message in the performance output file related to the
 *          monitoring of equation
 *
 * \param[in]  eqname    pointer to the name of the current equation
 * \param[in]  eqb       pointer to a cs_equation_builder_t structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_write_monitoring(const char                    *eqname,
                             const cs_equation_builder_t   *eqb)
{
  double t[6] = {eqb->tcb.wall_nsec, eqb->tcd.wall_nsec,
                 eqb->tca.wall_nsec, eqb->tcr.wall_nsec,
                 eqb->tcs.wall_nsec, eqb->tce.wall_nsec};
  for (int i = 0; i < 6; i++) t[i] *= 1e-9;

  if (eqname == NULL)
    cs_log_printf(CS_LOG_PERFORMANCE,
                  " %-35s %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f seconds\n",
                  "<CDO/Equation> Monitoring",
                  t[0], t[1], t[2], t[3], t[4], t[5]);
  else {
    char *msg = NULL;
    int len = 1 + strlen("<CDO/> Monitoring") + strlen(eqname);
    BFT_MALLOC(msg, len, char);
    sprintf(msg, "<CDO/%s> Monitoring", eqname);
    cs_log_printf(CS_LOG_PERFORMANCE,
                  " %-35s %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f seconds\n",
                  msg, t[0], t[1], t[2], t[3], t[4], t[5]);
    BFT_FREE(msg);
  }
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize all properties for an algebraic system
 *
 * \param[in]      eqp       pointer to a cs_equation_param_t structure
 * \param[in]      eqb       pointer to a cs_equation_builder_t structure
 * \param[in]      t_eval    time at which one performs the evaluation
 * \param[in, out] cb        pointer to a cs_cell_builder_t structure (diffusion
 *                           property is stored inside)
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_init_properties(const cs_equation_param_t     *eqp,
                            const cs_equation_builder_t   *eqb,
                            cs_real_t                      t_eval,
                            cs_cell_builder_t             *cb)
{
  /* Preparatory step for diffusion term */
  if (cs_equation_param_has_diffusion(eqp))
    if (eqb->diff_pty_uniform)
      /* One call this function as if it's a boundary cell */
      cs_equation_set_diffusion_property(eqp,
                                         0, /* cell_id */
                                         t_eval,
                                         CS_FLAG_BOUNDARY_CELL_BY_FACE,
                                         cb);

  /* Preparatory step for unsteady term */
  if (cs_equation_param_has_time(eqp))
    if (eqb->time_pty_uniform)
      cb->tpty_val = cs_property_get_cell_value(0, t_eval, eqp->time_property);

  /* Preparatory step for reaction term */
  if (cs_equation_param_has_reaction(eqp)) {

    for (int i = 0; i < CS_CDO_N_MAX_REACTIONS; i++) cb->rpty_vals[i] = 1.0;

    for (int r = 0; r < eqp->n_reaction_terms; r++) {
      if (eqb->reac_pty_uniform[r]) {
        cb->rpty_vals[r] =
          cs_property_get_cell_value(0, t_eval, eqp->reaction_properties[r]);
      }
    } /* Loop on reaction properties */

  }

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Initialize all properties for a given cell when building the
 *         algebraic system. If the property is uniform, a first call has to
 *         be done before the loop on cells
 *         Call \ref cs_eqution_init_properties for instance
 *
 * \param[in]      eqp        pointer to a cs_equation_param_t structure
 * \param[in]      eqb        pointer to a cs_equation_builder_t structure
 * \param[in]      t_eval     time at which one performs the evaluation
 * \param[in]      cell_flag  flag related to the current cell
 * \param[in]      cm         pointer to a cs_cell_mesh_t structure
 * \param[in, out] cb         pointer to a cs_cell_builder_t structure
 *                            (diffusion property is stored inside)
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_init_properties_cw(const cs_equation_param_t     *eqp,
                               const cs_equation_builder_t   *eqb,
                               const cs_real_t                t_eval,
                               const cs_flag_t                cell_flag,
                               const cs_cell_mesh_t          *cm,
                               cs_cell_builder_t             *cb)
{
  /* Set the diffusion property */
  if (cs_equation_param_has_diffusion(eqp))
    if (!(eqb->diff_pty_uniform))
      cs_equation_set_diffusion_property_cw(eqp, cm, t_eval, cell_flag, cb);

  /* Set the (linear) reaction property */
  if (cs_equation_param_has_reaction(eqp)) {

    /* Define the local reaction property */
    cb->rpty_val = 0;
    for (int r = 0; r < eqp->n_reaction_terms; r++)
      if (eqb->reac_pty_uniform[r])
        cb->rpty_val += cb->rpty_vals[r];
      else
        cb->rpty_val += cs_property_value_in_cell(cm,
                                                  eqp->reaction_properties[r],
                                                  t_eval);

  }

  if (cs_equation_param_has_time(eqp))
    if (!(eqb->time_pty_uniform))
      cb->tpty_val = cs_property_value_in_cell(cm, eqp->time_property, t_eval);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief   Take into account the enforcement of internal DoFs. Apply an
 *          algebraic manipulation
 *
 *          |      |     |     |      |     |     |  |     |             |
 *          | Aii  | Aie |     | Aii  |  0  |     |bi|     |bi -Aid.x_enf|
 *          |------------| --> |------------| and |--| --> |-------------|
 *          |      |     |     |      |     |     |  |     |             |
 *          | Aei  | Aee |     |  0   |  Id |     |be|     |   x_enf     |
 *
 * where x_enf is the value of the enforcement for the selected internal DoFs
 *
 * \param[in]       eqp       pointer to a \ref cs_equation_param_t struct.
 * \param[in, out]  cb        pointer to a cs_cell_builder_t structure
 * \param[in, out]  csys      structure storing the cell-wise system
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_enforced_internal_dofs(const cs_equation_param_t       *eqp,
                                   cs_cell_builder_t               *cb,
                                   cs_cell_sys_t                   *csys)
{
  /* Enforcement of the Dirichlet BCs */
  if (csys->has_internal_enforcement == false)
    return;  /* Nothing to do */

  double  *x_vals = cb->values;
  double  *ax = cb->values + csys->n_dofs;

  memset(cb->values, 0, 2*csys->n_dofs*sizeof(double));

  /* Build x_vals */
  for (short int i = 0; i < csys->n_dofs; i++) {
    if (csys->intern_forced_ids[i] > -1)
      x_vals[i] = eqp->enforced_dof_values[csys->intern_forced_ids[i]];
  }

  /* Contribution of the DoFs which are enforced */
  cs_sdm_matvec(csys->mat, x_vals, ax);

  /* Second pass: Replace the block of enforced DoFs by a diagonal block */
  for (short int i = 0; i < csys->n_dofs; i++) {

    if (csys->intern_forced_ids[i] > -1) {

      /* Reset row i */
      memset(csys->mat->val + csys->n_dofs*i, 0, csys->n_dofs*sizeof(double));
      /* Reset column i */
      for (short int j = 0; j < csys->n_dofs; j++)
        csys->mat->val[i + csys->n_dofs*j] = 0;
      csys->mat->val[i*(1 + csys->n_dofs)] = 1;

      /* Set the RHS */
      csys->rhs[i] = x_vals[i];

    } /* DoF associated to a Dirichlet BC */
    else
      csys->rhs[i] -= ax[i];  /* Update RHS */

  } /* Loop on degrees of freedom */

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Assemble a cellwise system into the global algebraic system
 *
 * \param[in]      csys     cellwise view of the algebraic system
 * \param[in]      rset     pointer to a cs_range_set_t structure
 * \param[in, out] mab      pointer to a matrix assembler buffers
 * \param[in, out] mav      pointer to a matrix assembler structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_assemble_matrix(const cs_cell_sys_t            *csys,
                            const cs_range_set_t           *rset,
                            cs_equation_assembly_buf_t     *mab,
                            cs_matrix_assembler_values_t   *mav)
{
  const cs_lnum_t  *const dof_ids = csys->dof_ids;
  const cs_sdm_t  *const m = csys->mat;
  const cs_real_t  *const mval = m->val;

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
  cs_timer_t  t0;
#if defined(HAVE_OPENMP) /* Determine the default number of OpenMP threads */
#pragma omp parallel
  {
    int  t_id = omp_get_thread_num();
    if (t_id == 0) t0 = cs_timer_time();
  }
#else
  t0 = cs_timer_time();
#endif
#endif

  /* Define the dof_gids */
  for (int i = 0; i < m->n_rows; i++)
    mab->dof_gids[i] = rset->g_id[dof_ids[i]];

  /* Assemble the matrix related to the advection/diffusion/reaction terms
     If advection is activated, the resulting system is not symmetric
     Otherwise, the system is symmetric with extra-diagonal terms. */

  int  bufsize = 0;
  for (int i = 0; i < m->n_rows; i++) {

    const cs_gnum_t  i_gid = mab->dof_gids[i];
    const cs_real_t  *const val_rowi = mval + i*m->n_rows;

    /* Diagonal term is excluded in this connectivity. Add it "manually" */
    for (int j = 0; j < m->n_rows; j++) {

      mab->row_gids[bufsize] = i_gid;
      mab->col_gids[bufsize] = mab->dof_gids[j];
      mab->values[bufsize] = val_rowi[j];
      bufsize += 1;

    } /* Loop on columns */

  } /* Loop on rows */

  assert(mab->buffer_size >= bufsize);
  if (bufsize > 0)
    cs_matrix_assembler_values_add_g(mav, bufsize,
                                     mab->row_gids, mab->col_gids, mab->values);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
#if defined(HAVE_OPENMP)
#pragma omp parallel reduction(+:n_assembly_value_calls)
  {
    int  t_id = omp_get_thread_num();
    if (t_id == 0) {
      cs_timer_t   t1 = cs_timer_time();
      cs_timer_counter_add_diff(&tcav, &t0, &t1);
    }
    n_assembly_value_calls++;
  }
#else
  cs_timer_t  t1 = cs_timer_time();
  cs_timer_counter_add_diff(&tcav, &t0, &t1);
  n_assembly_value_calls++;
#endif
#endif
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Assemble a cellwise system defined by blocks into the global
 *         algebraic system
 *
 * \param[in]      csys         cellwise view of the algebraic system
 * \param[in]      rset         pointer to a cs_range_set_t structure
 * \param[in, out] mab          pointer to a matrix assembler buffers
 * \param[in, out] mav          pointer to a matrix assembler structure
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_assemble_block_matrix(const cs_cell_sys_t            *csys,
                                  const cs_range_set_t           *rset,
                                  cs_equation_assembly_buf_t     *mab,
                                  cs_matrix_assembler_values_t   *mav)
{
  const cs_lnum_t  *dof_ids = csys->dof_ids;
  const int  n_x_dofs = mab->n_x_dofs;
  const cs_sdm_t  *m = csys->mat;
  const cs_sdm_block_t  *bd = m->block_desc;

  /* Sanity checks */
  assert(m->flag & CS_SDM_BY_BLOCK);
  assert(m->block_desc != NULL);
  assert(bd->n_row_blocks == bd->n_col_blocks);

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
  cs_timer_t  t0;
#if defined(HAVE_OPENMP)
#pragma omp parallel
  {
    if (omp_get_thread_num() == 0) t0 = cs_timer_time();
  }
#else
  t0 = cs_timer_time();
#endif
#endif

  /* Assemble the matrix related to the advection/diffusion/reaction terms
     If advection is activated, the resulting system is not symmetric
     Otherwise, the system is symmetric with extra-diagonal terms. */
  /* TODO: Add a symmetric version for optimization */

  int  bufsize = 0;
  for (int bi = 0; bi < bd->n_row_blocks; bi++) {

    /* dof_ids is an interlaced array (get access to the next n_x_dofs values */
    const cs_gnum_t  *const bi_gids = rset->g_id + dof_ids[n_x_dofs*bi];

    for (int bj = 0; bj < bd->n_col_blocks; bj++) {

      const cs_gnum_t  *const bj_gids = rset->g_id + dof_ids[n_x_dofs*bj];

      /* mIJ is a small square matrix of size n_x_dofs */
      const cs_sdm_t  *const mIJ = cs_sdm_get_block(m, bi, bj);

      for (short int ii = 0; ii < n_x_dofs; ii++) {

        const cs_gnum_t  i_gid = bi_gids[ii];
        const cs_real_t  *const val_rowi = mIJ->val + ii*n_x_dofs;

        for (short int jj = 0; jj < n_x_dofs; jj++) {

          /* Add an entry */
          mab->row_gids[bufsize] = i_gid;
          mab->col_gids[bufsize] = bj_gids[jj];
          mab->values[bufsize] = val_rowi[jj];
          bufsize += 1;

        } /* jj */
      } /* ii */

    } /* Loop on column blocks */
  } /* Loop on row blocks */

  assert(mab->buffer_size >= bufsize);
  if (bufsize > 0) {
    cs_matrix_assembler_values_add_g(mav, bufsize,
                                     mab->row_gids, mab->col_gids, mab->values);
  }

#if CS_EQUATION_COMMON_PROFILE_ASSEMBLY > 0 /* Profiling */
#if defined(HAVE_OPENMP)
#pragma omp parallel reduction(+:n_assembly_value_calls)
  {
    if (omp_get_thread_num() == 0) {
      cs_timer_t  t1 = cs_timer_time();
      cs_timer_counter_add_diff(&tcav, &t0, &t1);
    }
    n_assembly_value_calls++;
  }
#else
  cs_timer_t  t1 = cs_timer_time();
  cs_timer_counter_add_diff(&tcav, &t0, &t1);
  n_assembly_value_calls++;
#endif
#endif
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Get a pointer to a cs_equation_assembly_buf_t structure related
 *         to a given thread
 *
 * \param[in]  t_id    id in the array of pointer
 *
 * \return a pointer to a cs_equation_assembly_buf_t structure
 */
/*----------------------------------------------------------------------------*/

cs_equation_assembly_buf_t *
cs_equation_get_assembly_buffers(int    t_id)
{
  if (t_id < 0 || t_id >= cs_glob_n_threads)
    return NULL;

  return cs_assembly_buffers[t_id];
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Retrieve a pointer to a buffer of size at least the 2*n_cells
 *         The size of the temporary buffer can be bigger according to the
 *         numerical settings
 *
 * \return  a pointer to an array of double
 */
/*----------------------------------------------------------------------------*/

cs_real_t *
cs_equation_get_tmpbuf(void)
{
  return cs_equation_common_work_buffer;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Get the allocation size of the temporary buffer
 *
 * \return  the size of the temporary buffer
 */
/*----------------------------------------------------------------------------*/

size_t
cs_equation_get_tmpbuf_size(void)
{
  return cs_equation_common_work_buffer_size;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Allocate a cs_equation_balance_t structure
 *
 * \param[in]  location   where the balance is performed
 * \param[in]  size       size of arrays in the structure
 *
 * \return  a pointer to the new allocated structure
 */
/*----------------------------------------------------------------------------*/

cs_equation_balance_t *
cs_equation_balance_create(cs_flag_t    location,
                           cs_lnum_t    size)
{
  cs_equation_balance_t  *b = NULL;

  BFT_MALLOC(b, 1, cs_equation_balance_t);

  b->size = size;
  b->location = location;
  if (cs_flag_test(location, cs_flag_primal_cell) == false &&
      cs_flag_test(location, cs_flag_primal_vtx) == false)
    bft_error(__FILE__, __LINE__, 0, " %s: Invalid location", __func__);

  BFT_MALLOC(b->balance, 7*size, cs_real_t);
  b->unsteady_term  = b->balance +   size;
  b->reaction_term  = b->balance + 2*size;
  b->diffusion_term = b->balance + 3*size;
  b->advection_term = b->balance + 4*size;
  b->source_term    = b->balance + 5*size;
  b->boundary_term  = b->balance + 6*size;

  /* Set to zero all members */
  cs_equation_balance_reset(b);

  return b;
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Reset a cs_equation_balance_t structure
 *
 * \param[in, out] b     pointer to a cs_equation_balance_t to reset
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_balance_reset(cs_equation_balance_t   *b)
{
  if (b == NULL)
    return;
  if (b->size < 1)
    return;

  if (b->balance == NULL)
    bft_error(__FILE__, __LINE__, 0, " %s: array is not allocated.", __func__);

  size_t  bufsize = b->size *sizeof(cs_real_t);

  memset(b->balance, 0, 7*bufsize);
}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Synchronize balance terms if this is a parallel computation
 *
 * \param[in]      connect   pointer to a cs_cdo_connect_t structure
 * \param[in, out] b         pointer to a cs_equation_balance_t to rsync
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_balance_sync(const cs_cdo_connect_t    *connect,
                         cs_equation_balance_t     *b)
{
  if (cs_glob_n_ranks < 2)
    return;
  if (b == NULL)
    bft_error(__FILE__, __LINE__, 0, " %s: structure not allocated", __func__);

  if (cs_flag_test(b->location, cs_flag_primal_vtx)) {

    assert(b->size == connect->n_vertices);
    cs_interface_set_sum(connect->interfaces[CS_CDO_CONNECT_VTX_SCAL],
                         b->size,
                         7,   /* stride: 1 for each kind of balance */
                         false,
                         CS_REAL_TYPE,
                         b->balance);

  }

}

/*----------------------------------------------------------------------------*/
/*!
 * \brief  Free a cs_equation_balance_t structure
 *
 * \param[in, out]  p_balance  pointer to the pointer to free
 */
/*----------------------------------------------------------------------------*/

void
cs_equation_balance_destroy(cs_equation_balance_t   **p_balance)
{
  cs_equation_balance_t *b = *p_balance;

  if (b == NULL)
    return;

  BFT_FREE(b->balance);

  BFT_FREE(b);
  *p_balance = NULL;
}

/*----------------------------------------------------------------------------*/

END_C_DECLS
