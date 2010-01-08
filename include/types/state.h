/*
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 3.2.0
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * For more info, check our website at http://www.gromacs.org
 * 
 * And Hey:
 * GRoups of Organic Molecules in ACtion for Science
 */
#ifndef _state_h_
#define _state_h_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The t_state struct should contain all the (possibly) non-static
 * information required to define the state of the system.
 * Currently the random seeds for SD and BD are missing.
 */

/* for now, define the number of NH chains here */
#define NNHCHAINS 5

/* These enums are used in flags as (1<<est...).
 * The order of these enums should not be changed,
 * since that affects the checkpoint (.cpt) file format.
 */
  enum { estLAMBDA, estFEPSTATE,
	 estBOX, estBOX_REL, estBOXV, estPRES_PREV, estNH_XI,  estTC_INT,
	 estX,   estV,       estSDX,  estCGP,       estLD_RNG, estLD_RNGI,
	 estDISRE_INITF, estDISRE_RM3TAV,
	 estORIRE_INITF, estORIRE_DTAV,
	 estVIR_PREV, estNH_VXI, estVETA, estVOL0,
	 estNR };

#define EST_DISTR(e) (!(((e) >= estFEPSTATE && (e) <= estTC_INT) || ((e) >= estVIR_PREV && (e) <= estVOL0)))

/* The names of the state entries, defined in src/gmxib/checkpoint.c */
extern const char *est_names[estNR];

typedef struct
{
  real disre_initf;    /* The scaling factor for initializing the time av. */
  int  ndisrepairs;    /* The number of distance restraints                */
  real *disre_rm3tav;  /* The r^-3 time averaged pair distances            */
  real orire_initf;    /* The scaling factor for initializing the time av. */
  int  norire_Dtav;    /* The number of matrix element in dtav (npair*5)   */
  real *orire_Dtav;    /* The time averaged orientation tensors            */
} history_t;

/* Struct used for checkpointing only.
 * This struct would not be required with unlimited precision.
 * But because of limited precision, the COM motion removal implementation
 * can cause the kinetic energy in the MD loop to differ by a few bits from
 * the kinetic energy one would determine from state.v.
 */
typedef struct
{
  bool     bUpToDate;
  int      ekin_n;
  tensor  *ekinh;
  tensor  *ekinf;
  tensor  *ekinh_old;
  tensor   ekin_total;
  double  *ekinscalef_nhc;
  double  *ekinscaleh_nhc;
  double  *vscale_nhc;
  real     dekindl;
  real     mvcos;
} ekinstate_t;

typedef struct
{
  gmx_large_int_t nsteps;       /* The number of steps in the history            */
  gmx_large_int_t nsum;         /* The nr. of steps in the ener_ave and ener_sum */
  double *   ener_ave;     /* Energy term history sum to get fluctuations   */
  double *   ener_sum;     /* Energy term history sum to get fluctuations   */
  int        nener;        /* Number of energy terms in two previous arrays */
  gmx_large_int_t nsteps_sim;   /* The number of steps in ener_sum_sim           */
  gmx_large_int_t nsum_sim;     /* The number of frames in ener_sum_sim          */
  double *   ener_sum_sim; /* Energy term history sum of the whole sim      */
}
energyhistory_t;

typedef struct
{
  int           natoms;
  int           ngtc;
  int           nnhchains; /* number of nose-hoover chains               */
  int           nrng;
  int           nrngi;
  int           flags;  /* Flags telling which entries are present      */
  int           fep_state; /* which FEP state we are in                 */
  real          *lambda; /* lambda vector                        */
  matrix 	box;    /* box vector coordinates                      	*/
  matrix     	box_rel; /* Relitaive box vectors to preserve shape    	*/
  matrix 	boxv;   /* box velocitites for Parrinello-Rahman pcoupl */
  matrix        pres_prev; /* Pressure of the previous step for pcoupl  */
  matrix        vir_prev; /* Pressure of the previous step for pcoupl  */
  double        *nosehoover_xi;  /* for Nose-Hoover tcoupl (ngtc)       */
  double        *nosehoover_vxi; /* for N-H tcoupl (ngtc)               */
  double        *therm_integral; /* for N-H/V-rescale tcoupl (ngtc)     */
  real          veta; /* trotter based isotropic P-coupling             */
  real          vol0; /* initial volume,required for computing NPT conserverd quantity */
  int           nalloc; /* Allocation size for x, v and sd_x when !=NULL*/
  rvec          *x;     /* the coordinates (natoms)                     */
  rvec          *v;     /* the velocities (natoms)                      */
  rvec          *sd_X;  /* random part of the x update for stoch. dyn.  */
  rvec          *cg_p;  /* p vector for conjugate gradient minimization */

  unsigned int  *ld_rng;  /* RNG random state                           */
  int           *ld_rngi; /* RNG index                                  */

  history_t     hist;   /* Time history for restraints                  */

  ekinstate_t   ekinstate; /* The state of the kinetic energy data      */

  energyhistory_t  enerhist; /* Energy history for statistics           */
	
  int           ddp_count; /* The DD partitioning count for this state  */
  int           ddp_count_cg_gl; /* The DD part. count for index_gl     */
  int           ncg_gl; /* The number of local charge groups            */
  int           *cg_gl; /* The global cg number of the local cgs        */
  int           cg_gl_nalloc; /* Allocation size of cg_gl;              */
} t_state;

typedef struct 
{ 
  double *Qinv;  /* inverse mass of thermostat -- computed from inputs, but a good place to store */
  double Winv;   /* Pressure mass inverse -- computed, not input, but a good place to store. Need to make a matrix later */
  tensor Winvm;  /* inverse pressure mass tensor, computed       */       
} t_extmass;


typedef struct
{ 
  real veta;   
  double rscale;
  double vscale;
  double rvscale;
  double alpha;
  double *vscale_nhc;
} t_vetavars;

#ifdef __cplusplus
}
#endif


#endif /* _state_h_ */
