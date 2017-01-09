/*============================================================================
 * Code_Saturne documentation page
 *============================================================================*/

/*
  This file is part of Code_Saturne, a general-purpose CFD tool.

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
  Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

/*-----------------------------------------------------------------------------*/

/*!
  \page cs_user_postprocess Post-processing output (C functions)

  \section cs_user_postprocess_h_intro Introduction

  C user functions for definition of postprocessing output.
    These subroutines are called in all cases.

  If the Code_Saturne GUI is used, this file is not required (but may be
    used to override parameters entered through the GUI, and to set
    parameters not accessible through the GUI).

  Several functions are present in the file, each destined to defined
    specific parameters.

  The functions defined in \ref cs_user_postprocess.c, namely
  \ref cs_user_postprocess_writers, \ref cs_user_postprocess_meshes,
  \ref cs_user_postprocess_probes and
  \ref cs_user_postprocess_activate allow for the definition of post-processing
  output formats and frequency, and for the definition of surface or volume
  sections, in order to generate chronological outputs in \em EnSight, \em MED,
  or \em CGNS format, as in-situ visualization using \em Catalyst.

  Point sets (probes and profiles) may also be defined, with outputs in the more
  classical comma-separated (\em csv) or white-space-separated (\em dat) text
  files, in addition to the aforementioned output types.

  The main concepts are those of \em writers and \em meshes, which must be
  associated to produce outputs.

  A \em writer combines the definition of an output type, frequency, path, and name.
  One or more \em writers can be defined using the GUI and the
  \ref cs_user_postprocess_writers user function.

  A \em mesh is based on a subset of the the computational mesh, or point
  sets such as particles or probe sets. One or more \em meshes can be defined
  using the GUI and the \ref cs_user_postprocess_meshes user function.

  In order to allow the user to add an output format to the main output format,
  or to add a mesh to the default output, the lists of standard and user
  meshes and writers are not separated. Negative numbers are reserved for
  the non-user items. For instance, the mesh numbers -1 and -2 correspond
  respectively to the global mesh and to boundary faces, generated by default,
  and the writer -1 corresponds to the default post-processing writer.

  The user chooses the numbers corresponding to the post-processing
  meshes and writers he wants to create. These numbers must be positive
  integers. It is possible to associate a user mesh with the standard
  post-processing case (-1), or to ask for outputs regarding the boundary
  faces (-2) associated with a user writer.

  For safety, the output frequency and the possibility to modify the
  post-processing meshes are associated with the writers rather than
  with the meshes. This logic avoids unwanted generation of
  inconsistent post-processing outputs. For instance, \em EnSight would not
  be able to read a case in which one field is output to a given part
  every 10 time steps, while another field is output to the same part
  every 200 time steps.

  \section cs_user_postprocess_h_writers Definition of post-processing writers

  Writers may be defined in the \ref cs_user_postprocess_writers
  function.

  Flushing parameters for time plots may also be defined here. By default,
  for best performance, time plot files are kept open, and flushing is not
  forced. This behavior may be modified, as in the following example.
  The default settings should be changed before time plots are defined.

  \snippet cs_user_postprocess.c post_set_tp_flush

  The following local variable definitions can shared beween several examples:

  \snippet cs_user_postprocess.c post_define_writer_freq

  In the following example, the default writer is redefined, so as to modify
  some parameters which have been set by default or by the GUI:

  \snippet cs_user_postprocess.c post_define_writer_m1

  Polygons and polyhedra may also be divided into simpler elements, as in
  the example below (only for polyhedra here), using MED format output:

  \snippet cs_user_postprocess.c post_define_writer_1

  In the next example, the defined writer discards polyhedra, and
  allows for transient mesh connectivity (i.e. meshes changing with
  time); text output is also forced:

  \snippet cs_user_postprocess.c post_define_writer_2

  In this last example, a plot writers is defined. Such a writer
  will be used to output probe, profile, or other point data
  (probes may be output to 3D formats, but plot and time plot
  outputs drop cell or face-based ouptut).

  \snippet cs_user_postprocess.c post_define_writer_3

  \section cs_user_postprocess_h_mesh Definition of post-processing and mesh zones

  Postprocessing meshes may be defined in the
  \ref cs_user_postprocess_meshes function, using one of several postprocessing
  mesh creation functions (\ref cs_post_define_volume_mesh,
  \ref cs_post_define_volume_mesh_by_func,
  \ref cs_post_define_surface_mesh, \ref cs_post_define_surface_mesh_by_func,
  \ref cs_post_define_particles_mesh,
  \ref cs_post_define_particles_mesh_by_func, \ref cs_post_define_probe_mesh,
  \ref cs_post_define_existing_mesh, and \ref cs_post_define_edges_mesh).

  It is possible to output variables which are normally automatically
  output on the main volume or boundary meshes to a user mesh which is a subset
  of one of these by setting the \c auto_variables argument of
  one of the \c cs_post_define_..._mesh to \c true.

  It is not possible to mix cells and faces in the same mesh (most of
  the post-processing tools being perturbed by such a case). More precisely,
  faces adjacent to selected cells and belonging to face or cell groups
  may be selected when the \c add_groups of \c cs_post_define_..._mesh
  functions is set to \c true, so as to maintain group information, but those
  faces will only be written for formats supporting this (such as MED),
  and will only bear groups, not variable fields.

  The additional variables to post-process on the defined meshes
  will be specified in the subroutine \ref usvpst in the
  \ref cs_user_postprocess_var.f90 file.

  \warning In the parallel case, some meshes may not contain any
  local elements on a given processor. This is not a problem at all, as
  long as the mesh is defined for all processors (empty or not).
  It would in fact not be a good idea at all to define a post-processing
  mesh only if it contains local elements, global operations on that
  mesh would become impossible, leading to probable deadlocks or crashes.}

  \subsection cs_user_postprocess_h_mesh_reconf Example: reconfigure predefined meshes

  In the example below, the default boundary mesh output is suppressed by
  redefining it, with no writer association:

  \snippet cs_user_postprocess.c post_define_mesh_m2

  Note that the default behavior for meshes -1 and -2 is:
  \code{.C}
  int n_writers = 1;
  const int writer_ids[] = {-1});
  \endcode

  \subsection cs_user_postprocess_h_mesh_subset Example: select interior faces with y = 0.5

  In the following example, we use a geometric criteria to output only a subset of the
  main mesh, and associate the resulting mesh with user-defined writers 1 and 4:

  \snippet cs_user_postprocess.c post_define_mesh_1

  \section cs_user_postprocess_h_mesh_advanced Advanced definitions of post-processing and mesh zones

  More advanced mesh element selection is possible using
  \ref cs_post_define_volume_mesh_by_func or
  \ref cs_post_define_surface_mesh_by_func, which allow defining
  volume or surface meshes using user-defined element lists.

  The possibility to modify a mesh over time is limited by the most restrictive
  writer which is associated with. For instance, if writer 1 allows the
  modification of the mesh topology (argument \c time_dep
  = \ref FVM_WRITER_TRANSIENT_CONNECT in the call to \ref cs_post_define_writer)
  and writer 2 allows no modification (\c time_dep = \ref FVM_WRITER_FIXED_MESH),
  a user post-processing mesh associated with writers 1 and 2 will not be
  modifiable, but a mesh associated with writer 1 only will be modifiable. The
  modification can be done by using the advanced
  \ref cs_post_define_volume_mesh_by_func or
  \ref cs_post_define_surface_mesh_by_func, associated with a user-defined
  selection function based on time-varying criteria (such as field values
  being above a given threshold). If the \c time_dep argument is set to
  \c true, the mesh will be redefined using the selection function at
  each output time step for every modifiable mesh.

  \subsection cs_user_postprocess_h_mesh_a1 Example: surface mesh with complex selection criteria

  In the following example, we build a surface mesh containing interior faces
  separating cells of group "2" from those of group "3", (assuming no cell has
  both colors), as well as boundary faces of group "4".

  This is done by first defining 2 selection functions, whose arguments
  and behavior match the \ref cs_post_elt_select_t type.

  The function for selection of interior faces separating cells of two groups
  also illustrates the usage of the \ref cs_selector_get_family_list function
  to build a mask allowing direct checking of this criterion when comparing
  cells adjacent to a given face:

  \snippet cs_user_postprocess.c post_select_func_1

  The function for selection of boundary faces is simpler, as it simply
  needs to apply the selection criterion for boundary faces:

  \snippet cs_user_postprocess.c post_select_func_1

  Given these tow functions, the mesh can be defined using the
  \ref cs_post_define_surface_mesh_by_func function, passing it the
  user-defined selection functions (actually, function pointers):

  \snippet cs_user_postprocess.c post_define_mesh_3

  \subsection cs_user_postprocess_h_mesh_a2 Example: time-varying mesh

  A mesh defined through the advanced \ref cs_post_define_surface_mesh_by_func,
  \ref cs_post_define_volume_mesh_by_func, or
  \ref cs_post_define_particles_mesh_by_func may vary in time, as long as
  the matching \c time_varying argument is set to \c true, and the mesh
  (or aliases thereof) id only associated to writers defined with the
  \ref FVM_WRITER_TRANSIENT_CONNECT option. In the case of particles,
  which always vary in time, this allows also varying the selection (filter)
  function with time.

  In the following example, we build a volume mesh containing cells
  with values of field named "He_fraction" greater than 0.05.

  First, we define the selection function:

  \snippet cs_user_postprocess.c post_select_func_3

  Then, we simply define matching volume mesh passing the associated
  selection function pointer:

  \snippet cs_user_postprocess.c post_define_mesh_4

  The matching function will be called at all time steps requiring
  output of this mesh.

  \warning some mesh formats do not allow changing meshes (or the
  implemented output functions do not allow them yet) and some may
  not allow empty meshes, even if this is only transient.

  \subsection cs_user_postprocess_h_mesh_a3 Example: edges mesh

  In cases where a mesh containing polygonal elements is output through
  a writer configured to divide polygons into triangles (for example when
  visualization tools do not support polygons, or when highly non convex
  faces lead to visualization artifacts), it may be useful to extract a
  mesh containing the edges of the original mesh so as to view the polygon
  boundaries as an overlay.

  In the following example, we build such a mesh (with id 5), based on the
  faces of a mesh with id 1:

  \snippet cs_user_postprocess.c post_define_mesh_5

  \section cs_user_postprocess_h_activation Management of output times

  By default, a post-processing frequency is defined for each writer,
  as defined using the GUI or through the \ref cs_user_postprocess_writers
  function. For each writer, the user may define if an output is
  automatically generated at the end of the calculation, even if the
  last time step is not a multiple of the required time step number
  of physical time.

  For finer control, the \ref cs_user_postprocess_activate function
  file may be used to specify when post-processing outputs will be
  generated, overriding the default behavior.

  In the following example, all output is deactivated until time step 1000
  is reached, at which time the normal behavior resumes:

  \snippet cs_user_postprocess.c post_activate

  \section cs_user_postprocess_h_probes Probes

  Sets of probes may also be defined through the \ref cs_user_postprocess_probes
  function, to allow for extraction and output of values at specific mesh
  locations, often with a higher time frequency than for volume or
  surface meshes.

  Probe sets, and profiles (which can be viewed as a series of probes
  lying on a user-defined curve) are handled as a point mesh, which can
  be associated with \em plot and \em time_plot 2D-plot writers, as well as
  any of the general (3D-output) writer types (the priviledged writer
  types being \em plot for a \profile, and \em time_plot for other \em probes).

  Probe sets may be defined using the \ref cs_probe_set_create_from_array
  function. In some cases, it might be useful to define those in
  multiple stages, using first \ref cs_probe_set_create then a series
  of calls to \ref cs_probe_set_add_probe.

  The following example shows how probes may be added using that function.

  \snippet cs_user_postprocess.c post_define_probes_1

  Probe set creation functions return a pointer to a \ref cs_probe_set_t
  structure which can be used to specify additional options using the
  \ref cs_probe_set_option function.

  A writer (id = CS_POST_WRITER_PROBES) using the format "time_plot" is
  associated by default to a set of monitoring probes.
  This is not the case for profiles.

  \section cs_user_postprocess_h_var Definition of the variables to post-process

  For the mesh parts defined using the GUI or in \ref cs_user_postprocess.c,
  the \ref cs_user_postprocess_values function  of the \ref cs_user_postprocess.c
  file may be used to specify the variables to post-process (called for each
  postprocess output mesh, at every active time step of an associated \em writer).

  The output of a given variable is generated by means of a call to the
  \ref cs_post_write_var for cell or face values, \ref cs_post_write_vertex_var
  for vertex values, \cs_post_write_particle_var for particle or trjectory
  values, and \ref cs_post_write_probe_values for probe or profile values.

  The examples of post-processing given below use meshes defined in the
  examples for \cs_user_post_process_mesh above.

  \subsection cs_user_postprocess_h_volume_mesh_tke Output of the turbulent kinetic energy for the Rij-Epsilon model on the volume mesh

  One can define, compute and post-process the turbulent kinetic energy for the Rij-Epsilon
  as shown in the following example:

  \snippet cs_user_postprocess.c postprocess_values_ex_1
*/