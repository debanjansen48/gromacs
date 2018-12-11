/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2013,2014,2015, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
/* This file is completely threadsafe - keep it that way! */
#include "gmxpre.h"

#include "gen_ad.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>

#include <algorithm>

#include "gromacs/fileio/confio.h"
#include "gromacs/gmxpreprocess/gpp_nextnb.h"
#include "gromacs/gmxpreprocess/notset.h"
#include "gromacs/gmxpreprocess/pgutil.h"
#include "gromacs/gmxpreprocess/resall.h"
#include "gromacs/gmxpreprocess/topio.h"
#include "gromacs/gmxpreprocess/toputil.h"
#include "gromacs/math/vec.h"
#include "gromacs/topology/ifunc.h"
#include "gromacs/utility/cstringutil.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/smalloc.h"

static gmx_bool is_hydro(t_atoms *atoms, int ai)
{
    return ((*(atoms->atomname[ai]))[0] == 'H');
}

static gmx_bool is_d(t_atoms *atoms, int ai)
{
    return ((*(atoms->atomname[ai]))[0] == 'D');
}

static gmx_bool is_lp(t_atoms *atoms, int ai)
{
    return ((*(atoms->atomname[ai]))[0] == 'L');
}

static gmx_bool is_real_atom(t_atoms *atoms, int ai)
{
    gmx_bool    bReal;
    /* Return true if it is not a Drude or LP, i.e. an actual atom */
    /* bReal = atoms->atom[ai].ptype == eptAtom; */
    bReal = !is_d(atoms, ai) && !is_lp(atoms, ai); 
    return bReal;
}

#define DIHEDRAL_WAS_SET_IN_RTP 0
static gmx_bool was_dihedral_set_in_rtp(t_param *dih)
{
    return dih->c[MAXFORCEPARAM-1] == DIHEDRAL_WAS_SET_IN_RTP;
}

typedef gmx_bool (*peq)(t_param *p1, t_param *p2);


typedef struct {
    int ai, aj;
} sortable;

static int bond_sort (const void *a, const void *b)
{
    sortable *sa, *sb;

    sa = (sortable *) a;
    sb = (sortable *) b;

    if (sa->ai == sb->ai)
    {
        return (sa->aj-sb->aj);
    }
    else
    {
        return (sa->ai-sb->ai);
    }
}

static int acomp(const void *a1, const void *a2)
{
    t_param *p1, *p2;
    int      ac;

    p1 = (t_param *)a1;
    p2 = (t_param *)a2;
    if ((ac = (p1->aj()-p2->aj())) != 0)
    {
        return ac;
    }
    else if ((ac = (p1->ai()-p2->ai())) != 0)
    {
        return ac;
    }
    else
    {
        return (p1->ak()-p2->ak());
    }
}

static int pcomp(const void *a1, const void *a2)
{
    t_param *p1, *p2;
    int      pc;

    p1 = (t_param *)a1;
    p2 = (t_param *)a2;
    if ((pc = (p1->ai()-p2->ai())) != 0)
    {
        return pc;
    }
    else
    {
        return (p1->aj()-p2->aj());
    }
}

static int dcomp(const void *d1, const void *d2)
{
    t_param *p1, *p2;
    int      dc;

    p1 = (t_param *)d1;
    p2 = (t_param *)d2;
    /* First sort by J & K (the two central) atoms */
    if ((dc = (p1->aj()-p2->aj())) != 0)
    {
        return dc;
    }
    else if ((dc = (p1->ak()-p2->ak())) != 0)
    {
        return dc;
    }
    /* Then make sure to put rtp dihedrals before generated ones */
    else if (was_dihedral_set_in_rtp(p1) &&
             !was_dihedral_set_in_rtp(p2))
    {
        return -1;
    }
    else if (!was_dihedral_set_in_rtp(p1) &&
             was_dihedral_set_in_rtp(p2))
    {
        return 1;
    }
    /* Finally, sort by I and J (two outer) atoms */
    else if ((dc = (p1->ai()-p2->ai())) != 0)
    {
        return dc;
    }
    else
    {
        return (p1->al()-p2->al());
    }
}


static gmx_bool is_dihedral_on_same_bond(t_param *p1, t_param *p2)
{
    if (((p1->aj() == p2->aj()) && (p1->ak() == p2->ak())) ||
        ((p1->aj() == p2->ak()) && (p1->ak() == p2->aj())))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}


static gmx_bool preq(t_param *p1, t_param *p2)
{
    if ((p1->ai() == p2->ai()) && (p1->aj() == p2->aj()))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

static void rm2par(t_param p[], int *np, peq eq)
{
    int *index, nind;
    int  i, j;

    if ((*np) == 0)
    {
        return;
    }

    snew(index, *np);
    nind          = 0;
    index[nind++] = 0;
    for (i = 1; (i < (*np)); i++)
    {
        if (!eq(&p[i], &p[i-1]))
        {
            index[nind++] = i;
        }
    }
    /* Index now holds pointers to all the non-equal params,
     * this only works when p is sorted of course
     */
    for (i = 0; (i < nind); i++)
    {
        for (j = 0; (j < MAXATOMLIST); j++)
        {
            p[i].a[j] = p[index[i]].a[j];
        }
        for (j = 0; (j < MAXFORCEPARAM); j++)
        {
            p[i].c[j] = p[index[i]].c[j];
        }
        if (p[index[i]].a[0] == p[index[i]].a[1])
        {
            if (debug)
            {
                fprintf(debug,
                        "Something VERY strange is going on in rm2par (gen_ad.c)\n"
                        "a[0] %d a[1] %d a[2] %d a[3] %d\n",
                        p[i].a[0], p[i].a[1], p[i].a[2], p[i].a[3]);
            }
            strcpy(p[i].s, "");
        }
        else if (index[i] > i)
        {
            /* Copy the string only if it comes from somewhere else
             * otherwise we will end up copying a random (newly freed) pointer.
             * Since the index is sorted we only have to test for index[i] > i.
             */
            strcpy(p[i].s, p[index[i]].s);
        }
    }
    (*np) = nind;

    sfree(index);
}

static void cppar(t_param p[], int np, t_params plist[], int ftype)
{
    int       i, j, nral, nrfp;
    t_params *ps;

    ps   = &plist[ftype];
    nral = NRAL(ftype);
    nrfp = NRFP(ftype);

    /* Keep old stuff */
    pr_alloc(np, ps);
    for (i = 0; (i < np); i++)
    {
        for (j = 0; (j < nral); j++)
        {
            ps->param[ps->nr].a[j] = p[i].a[j];
        }
        for (j = 0; (j < nrfp); j++)
        {
            ps->param[ps->nr].c[j] = p[i].c[j];
        }
        for (j = 0; (j < MAXSLEN); j++)
        {
            ps->param[ps->nr].s[j] = p[i].s[j];
        }
        ps->nr++;
    }
}

static void cpparam(t_param *dest, t_param *src)
{
    int j;

    for (j = 0; (j < MAXATOMLIST); j++)
    {
        dest->a[j] = src->a[j];
    }
    for (j = 0; (j < MAXFORCEPARAM); j++)
    {
        dest->c[j] = src->c[j];
    }
    for (j = 0; (j < MAXSLEN); j++)
    {
        dest->s[j] = src->s[j];
    }
}

static void set_p(t_param *p, int ai[4], real *c, char *s)
{
    int j;

    for (j = 0; (j < 4); j++)
    {
        p->a[j] = ai[j];
    }
    for (j = 0; (j < MAXFORCEPARAM); j++)
    {
        if (c)
        {
            p->c[j] = c[j];
        }
        else
        {
            p->c[j] = NOTSET;
        }
    }

    set_p_string(p, s);
}

/* generalized version of set_p() for other bonded interactions */
static void set_p_tdb(t_param *p, int ai[MAXATOMLIST], real *c, char *s, int n)
{
    int j;

    for (j = 0; (j < n); j++)
    {
        if (debug)
        {
            fprintf(debug, "In set_p_tdb: j = %d, ai[j] = %d\n", j, ai[j]);
        }
        p->a[j] = ai[j];
    }
    for (j = 0; (j < MAXFORCEPARAM); j++)
    {
        if (c)
        {
            p->c[j] = c[j];
        }
        else
        {
            p->c[j] = NOTSET;
        }
    }

    set_p_string(p, s);
}

static int int_comp(const void *a, const void *b)
{
    return (*(int *)a) - (*(int *)b);
}

static int idcomp(const void *a, const void *b)
{
    t_param *pa, *pb;
    int      d;

    pa = (t_param *)a;
    pb = (t_param *)b;
    if ((d = (pa->a[0]-pb->a[0])) != 0)
    {
        return d;
    }
    else if ((d = (pa->a[3]-pb->a[3])) != 0)
    {
        return d;
    }
    else if ((d = (pa->a[1]-pb->a[1])) != 0)
    {
        return d;
    }
    else
    {
        return (int) (pa->a[2]-pb->a[2]);
    }
}

static void sort_id(int nr, t_param ps[])
{
    int i, tmp;

    /* First swap order of atoms around if necessary */
    for (i = 0; (i < nr); i++)
    {
        if (ps[i].a[3] < ps[i].a[0])
        {
            tmp = ps[i].a[3]; ps[i].a[3] = ps[i].a[0]; ps[i].a[0] = tmp;
            tmp = ps[i].a[2]; ps[i].a[2] = ps[i].a[1]; ps[i].a[1] = tmp;
        }
    }
    /* Now sort it */
    if (nr > 1)
    {
        qsort(ps, nr, (size_t)sizeof(ps[0]), idcomp);
    }
}

static int n_hydro(int a[], char ***atomname)
{
    int  i, nh = 0;
    char c0, c1, *aname;

    for (i = 0; (i < 4); i += 3)
    {
        aname = *atomname[a[i]];
        c0    = toupper(aname[0]);
        if (c0 == 'H')
        {
            nh++;
        }
        else if (((int)strlen(aname) > 1) && (c0 >= '0') && (c0 <= '9'))
        {
            c1 = toupper(aname[1]);
            if (c1 == 'H')
            {
                nh++;
            }
        }
    }
    return nh;
}

/* Clean up angles - only really necessary for Drude FF */
static void clean_ang(t_param *ang, int *nang, t_atoms *atoms)
{

    int i, j;
    int *index, nind;

    /* construct list of angle indices */
    snew(index, *nang+1);
    nind = *nang;
    for (i = 0; i < nind; i++)
    {
        index[i] = i;
    }
    index[nind] = *nang;

    /* loop over angles and remove any we don't want to keep,
     * i.e. those with a Drude or LP at atom ai or ak */
    j = 0;
    for (i = 0; i < nind; i++)
    {
        gmx_bool    bKeep = TRUE;

        if (is_d(atoms, ang[index[i]].ai()) || is_d(atoms, ang[index[i]].ak()) ||
            is_lp(atoms, ang[index[i]].ai()) || is_lp(atoms, ang[index[i]].ak()))
        {
            bKeep = FALSE;
        }

        if (bKeep)
        {
            cpparam(&ang[j], &ang[index[i]]);
            j++;
        }
    }

    for (i = j; i < *nang; i++)
    {
        strcpy(ang[i].s, "");
    }
    *nang = j;

    sfree(index);
}

/* Clean up the dihedrals (both generated and read from the .rtp
 * file). */
static void clean_dih(t_param *dih, int *ndih, t_param improper[], int nimproper,
                      t_atoms *atoms, gmx_bool bKeepAllGeneratedDihedrals,
                      gmx_bool bRemoveDihedralIfWithImproper)
{
    int   i, j, k, l;
    int  *index, nind;

    /* Construct the list of the indices of the dihedrals
     * (i.e. generated or read) that might be kept. */
    snew(index, *ndih+1);
    if (bKeepAllGeneratedDihedrals)
    {
        fprintf(stderr, "Keeping all generated dihedrals\n");
        nind = *ndih;
        for (i = 0; i < nind; i++)
        {
            index[i] = i;
        }
        index[nind] = *ndih;
    }
    else
    {
        nind = 0;
        /* Check if generated dihedral i should be removed. The
         * dihedrals have been sorted by dcomp() above, so all those
         * on the same two central atoms are together, with those from
         * the .rtp file preceding those that were automatically
         * generated. We remove the latter if the former exist. */
        for (i = 0; i < *ndih; i++)
        {
            /* Keep the dihedrals that were defined in the .rtp file,
             * and the dihedrals that were generated and different
             * from the last one (whether it was generated or not). */
            if (was_dihedral_set_in_rtp(&dih[i]) ||
                0 == i ||
                !is_dihedral_on_same_bond(&dih[i], &dih[i-1]))
            {
                index[nind++] = i;
            }
        }
        index[nind] = *ndih;
    }

    k = 0;
    for (i = 0; i < nind; i++)
    {
        gmx_bool bWasSetInRTP = was_dihedral_set_in_rtp(&dih[index[i]]);
        gmx_bool bKeep        = TRUE;
        if (!bWasSetInRTP && bRemoveDihedralIfWithImproper)
        {
            /* Remove the dihedral if there is an improper on the same
             * bond. */
            for (j = 0; j < nimproper && bKeep; j++)
            {
                bKeep = !is_dihedral_on_same_bond(&dih[index[i]], &improper[j]);
            }
        }

        /* remove dihedral if ai or al is a Drude or LP */
        if (is_d(atoms, dih[index[i]].ai()) || is_d(atoms, dih[index[i]].al()) ||
            is_lp(atoms, dih[index[i]].ai()) || is_lp(atoms, dih[index[i]].al()))
        {
            bKeep = FALSE;
        }

        if (bKeep)
        {
            /* If we don't want all dihedrals, we want to select the
             * ones with the fewest hydrogens. Note that any generated
             * dihedrals on the same bond as an .rtp dihedral may have
             * been already pruned above in the construction of
             * index[]. However, their parameters are still present,
             * and l is looping over this dihedral and all of its
             * pruned siblings. */
            int bestl = index[i];
            if (!bKeepAllGeneratedDihedrals && !bWasSetInRTP)
            {
                /* Minimum number of hydrogens for i and l atoms */
                int minh = 2;
                for (l = index[i];
                     (l < index[i+1] &&
                      is_dihedral_on_same_bond(&dih[index[i]], &dih[l]));
                     l++)
                {
                    int nh = n_hydro(dih[l].a, atoms->atomname);
                    if (nh < minh)
                    {
                        minh  = nh;
                        bestl = l;
                    }
                    if (0 == minh)
                    {
                        break;
                    }
                }
            }
            if (k != bestl)
            {
                cpparam(&dih[k], &dih[bestl]);
            }
            k++;
        }
    }

    for (i = k; i < *ndih; i++)
    {
        strcpy(dih[i].s, "");
    }
    *ndih = k;

    sfree(index);
}

/* In reality, this could probably be merged with get_impropers, but most people
 * probably aren't using polarizable FF, so to keep things separate and clean, I
 * added this as a separate function - jal */
static int get_tdb_bonded(t_atoms *atoms, t_hackblock hb[], t_param **p, int ftype)
{

    const char *ptr;
    int         nbonded, i, j, k, start, ninc, nalloc;
    int         btype;
    int         natoms;
    t_rbondeds *bondeds;
    int         ai[MAXATOMLIST];
    gmx_bool    bStop;

    ninc = 100;     /* There should usually be very few of these to deal with */
    nalloc = ninc;
    snew(*p, nalloc);

    nbonded = 0;
    start = 0;

    /* determine how many atoms to look for in each of the possible bonded types */
    switch (ftype)
    {
        case F_THOLE_POL:
            natoms = 4;
            btype = ebtsTHOLE;
            ptr = "thole polarization";
            break;
        case F_ANISO_POL:
            natoms = 5;
            btype = ebtsANISO;
            ptr = "anisotropic polarization";
            break;
        case F_POLARIZATION:
            natoms = 2;
            btype = ebtsPOL;
            ptr = "polarization";
            break;
        case F_VSITE3:
            natoms = 4;
            btype = ebtsVSITE3;
            ptr = "virtual sites";
            break;
        default:
            gmx_fatal(FARGS, "Unknown function type passed to get_tdb_bonded().\n");
    }

    if (debug)
    {
        ptr = ptr;
    }
    else
    {
        ptr = "check";
    }

    if (hb != NULL)
    {
        for (i = 0; (i < atoms->nres); i++)
        {
            bondeds = &hb[i].rb[btype];
            for (j = 0; (j < bondeds->nb); j++)
            {
                bStop = FALSE;
                for (k = 0; (k < natoms) && !bStop; k++)
                {
                    /* allow for missing atoms */
                    ai[k] = search_atom(bondeds->b[j].a[k], start, atoms, ptr, TRUE);
                    if (ai[k] == -1)
                    {
                        bStop = TRUE;
                    }
                    if (debug)
                    {
                        if (!bStop)
                        {
                            fprintf(debug, "Atom found in get_tdb_bonded: k = %d, ai[k] = %d\n", k, ai[k]);
                        }
                    }
                }
                if (!bStop)
                {
                    if (nbonded == nalloc)
                    {
                        nalloc += ninc;
                        srenew(*p, nalloc);
                    }
                    set_p_tdb(&((*p)[nbonded]), ai, NULL, bondeds->b[j].s, natoms);
                    nbonded++;
                }
            }
            while ((start < atoms->nr) && (atoms->atom[start].resind == i))
            {
                start++;
            }
        }
    }

    return(nbonded);
}

static int get_impropers(t_atoms *atoms, t_hackblock hb[], t_param **improper,
                         gmx_bool bAllowMissing)
{
    t_rbondeds   *impropers;
    int           nimproper, i, j, k, start, ninc, nalloc;
    int           ai[MAXATOMLIST];
    gmx_bool      bStop;

    ninc   = 500;
    nalloc = ninc;
    snew(*improper, nalloc);

    /* Add all the impropers from the residue database to the list. */
    nimproper = 0;
    start     = 0;
    if (hb != NULL)
    {
        for (i = 0; (i < atoms->nres); i++)
        {
            impropers = &hb[i].rb[ebtsIDIHS];
            for (j = 0; (j < impropers->nb); j++)
            {
                bStop = FALSE;
                for (k = 0; (k < 4) && !bStop; k++)
                {
                    ai[k] = search_atom(impropers->b[j].a[k], start,
                                        atoms,
                                        "improper", bAllowMissing);
                    if (ai[k] == -1)
                    {
                        bStop = TRUE;
                    }
                }
                if (!bStop)
                {
                    if (nimproper == nalloc)
                    {
                        nalloc += ninc;
                        srenew(*improper, nalloc);
                    }
                    /* Not broken out */
                    set_p(&((*improper)[nimproper]), ai, NULL, impropers->b[j].s);
                    nimproper++;
                }
            }
            while ((start < atoms->nr) && (atoms->atom[start].resind == i))
            {
                start++;
            }
        }
    }

    return nimproper;
}

static int nb_dist(t_nextnb *nnb, int ai, int aj)
{
    int  nre, nrx, NRE;
    int *nrexcl;
    int *a;

    if (ai == aj)
    {
        return 0;
    }

    NRE    = -1;
    nrexcl = nnb->nrexcl[ai];
    for (nre = 1; (nre < nnb->nrex); nre++)
    {
        a = nnb->a[ai][nre];
        for (nrx = 0; (nrx < nrexcl[nre]); nrx++)
        {
            if ((aj == a[nrx]) && (NRE == -1))
            {
                NRE = nre;
            }
        }
    }
    return NRE;
}

static void get_atomnames_min(int n, char **anm,
                              int resind, t_atoms *atoms, int *a)
{
    int m;

    /* Assume ascending residue numbering */
    for (m = 0; m < n; m++)
    {
        if (atoms->atom[a[m]].resind < resind)
        {
            strcpy(anm[m], "-");
        }
        else if (atoms->atom[a[m]].resind > resind)
        {
            strcpy(anm[m], "+");
        }
        else
        {
            strcpy(anm[m], "");
        }
        strcat(anm[m], *(atoms->atomname[a[m]]));
    }
}

/* Generate exclusions for Drudes and LP based on the two input atoms
 * that are identified as real atoms in gen_pad() */
static void gen_drude_lp_excl(t_params plist[], t_atoms *atoms, t_excls *excls, int ai, int aj)
{
    int i, j, f;

    /* The two atoms passed to this function are already excluded from one another, 
     * so here we only build exclusions explicitly involving Drudes and LP, so we
     * do the same operations on both atoms */

    /* Always check against atoms->nr to make sure the atom is in bounds to avoid seg faults */

    if ((aj+1) < atoms->nr)
    {
        /* Polarizable atom found */
        if (is_d(atoms, (aj+1)))
        {
            /* atom - Drude */
            srenew(excls[ai].e, excls[ai].nr+1);
            excls[ai].e[excls[ai].nr] = (aj+1);
            excls[ai].nr++;
            /* Drude - Drude */
            if ((ai+1) < atoms->nr)
            {
                if (is_d(atoms, (ai+1)))
                {
                    srenew(excls[ai+1].e, excls[ai+1].nr+1);
                    excls[ai+1].e[excls[ai+1].nr] = (aj+1);
                    excls[ai+1].nr++;
                }
            }
            /* Here, we loop over all possible vsite function types and add
             * an exclusion if the parent atom (plist[F_*].param[i].aj()) is aj */
            for (f = F_VSITE2; f <= F_VSITE4FDN; f++)
            {
                for (i = 0; i < plist[f].nr; i++)
                {
                    /* atom aj is a host atom of at least one LP */
                    if (aj == plist[f].param[i].aj())
                    {
                        /* atom - LP */
                        /* index ai() in plist is the actual LP, not to be confused
                         * with ai, which is the atom onto which we are adding exclusions */
                        srenew(excls[ai].e, excls[ai].nr+1);
                        excls[ai].e[excls[ai].nr] = plist[f].param[i].ai();
                        excls[ai].nr++;
                        /* Drude - LP */
                        if ((ai+1) < atoms->nr)
                        {
                            if (is_d(atoms, (ai+1)))
                            {
                                srenew(excls[ai+1].e, excls[ai+1].nr+1);
                                excls[ai+1].e[excls[ai+1].nr] = plist[f].param[i].ai();
                                excls[ai+1].nr++;
                            }
                        }
                        /* LP with Drude on host atom */
                        if ((aj+1) < atoms->nr)
                        {
                            if (is_d(atoms, (aj+1)))
                            {
                                srenew(excls[aj+1].e, excls[aj+1].nr+1);
                                excls[aj+1].e[excls[aj+1].nr] = plist[f].param[i].ai();
                                excls[aj+1].nr++;
                            }
                        }
                        /* LP with the host atom */
                        srenew(excls[aj].e, excls[aj].nr+1);
                        excls[aj].e[excls[aj].nr] = plist[f].param[i].ai();
                        excls[aj].nr++;
                        /* LP - LP */
                        /* By looping over all combinations, we take care of LP-LP on same atom,
                         * and all combinations of LP-LP on excluded atoms */ 
                        /* TODO: TESTING, was j=0 */
                        for (j = i; j < plist[f].nr; j++)
                        {
                            /* we know that aj is a LP host, so if param.aj() is ai,
                             * then we have a LP-LP combination that needs an exclusion */ 
                            if ((ai == plist[f].param[j].aj()) || (aj == plist[f].param[j].aj()))
                            {
                                /* no need to exclude self */
                                if (plist[f].param[i].ai() != plist[f].param[j].ai())
                                {
                                    srenew(excls[plist[f].param[i].ai()].e, excls[plist[f].param[i].ai()].nr+1);
                                    excls[plist[f].param[i].ai()].e[excls[plist[f].param[i].ai()].nr] = plist[f].param[j].ai();
                                    excls[plist[f].param[i].ai()].nr++;
                                }
                            }
                        }
                    }
                }   /* end of i-loop */
            }   /* end of f-loop */
        }
    }

    /* This check shouldn't be necessary as ai < aj due to number swap in
     * construct_drude_lp(), but just in case */
    if ((ai+1) < atoms->nr)
    {
        /* Polarizable atom found */
        if (is_d(atoms, (ai+1)))
        {
            /* Drude - atom */
            srenew(excls[ai+1].e, excls[ai+1].nr+1);
            excls[ai+1].e[excls[ai+1].nr] = aj;
            excls[ai+1].nr++;
            /* Drude - Drude */
            if ((aj+1) < atoms->nr)
            {
                if (is_d(atoms, (aj+1)))
                {
                    srenew(excls[aj+1].e, excls[aj+1].nr+1);
                    excls[aj+1].e[excls[aj+1].nr] = (ai+1);
                    excls[aj+1].nr++;
                }
            }
            /* as above, loop over vsite function types */
            for (f = F_VSITE2; f <= F_VSITE4FDN; f++)
            {
                for (i = 0; i < plist[f].nr; i++)
                {
                    /* atom ai is a host atom of at least one LP */
                    if (ai == plist[f].param[i].aj())
                    {
                        /* LP - atom */
                        /* index ai() in plist is the actual LP, not to be confused
                         * with ai, which is the atom onto which we are adding exclusions */
                        srenew(excls[aj].e, excls[aj].nr+1);
                        excls[aj].e[excls[aj].nr] = plist[f].param[i].ai();
                        excls[aj].nr++;
                        /* LP - Drude */
                        if ((aj+1) < atoms->nr)
                        {
                            if (is_d(atoms, (aj+1)))
                            {
                                srenew(excls[aj+1].e, excls[aj+1].nr+1);
                                excls[aj+1].e[excls[aj+1].nr] = plist[f].param[i].ai();
                                excls[aj+1].nr++;
                            }
                        }
                        /* LP with Drude on host atom */
                        if ((ai+1) < atoms->nr)
                        {
                            if (is_d(atoms, (ai+1)))
                            {
                                srenew(excls[ai+1].e, excls[ai+1].nr+1);
                                excls[ai+1].e[excls[ai+1].nr] = plist[f].param[i].ai();
                                excls[ai+1].nr++;
                            }
                        }
                        /* LP with the host atom */
                        srenew(excls[ai].e, excls[ai].nr+1);
                        excls[ai].e[excls[ai].nr] = plist[f].param[i].ai();
                        excls[ai].nr++;
                        /* LP - LP, as above */
                        /* TODO: TESTING, was j=0 */
                        for (j = i; j < plist[f].nr; j++)
                        {
                            /* check for self and excluded LP-LP interactions */
                            if ((aj == plist[f].param[j].aj()) || (ai == plist[f].param[j].aj()))
                            {
                                /* no need to exclude self */
                                if (plist[f].param[i].ai() != plist[f].param[j].ai())
                                {
                                    srenew(excls[plist[f].param[i].ai()].e, excls[plist[f].param[i].ai()].nr+1);
                                    excls[plist[f].param[i].ai()].e[excls[plist[f].param[i].ai()].nr] = plist[f].param[j].ai();
                                    excls[plist[f].param[i].ai()].nr++;
                                }
                            }
                        }
                    }
                }   /* end i-loop */
            }   /* end f-loop */
        }
    }
}

/* Uses the nnb structure to generate bonded exclusions, and subsequently
 * calls gen_drude_lp_excl to update t_excls */
void construct_drude_lp_excl(t_nextnb *nnb, t_params plist[], t_atoms *atoms, t_excls *excls)
{
    int i, j, i1, i2, itmp;

    int nre, nrs, nrx;  /* counters */
    int j_index;        /* just a counter */
    int nex;            /* total number of exclusions */
    sortable *s;

    /* sort and remove duplicates using nnb */
    for (i = 0; i < nnb->nr; i++)
    {
        nex = 0;
        /* start with j=1 because we don't care about self exclusions */
        for (j = 1; j < nnb->nrex; j++)
        {
            nex += nnb->nrexcl[i][j];
        }    

        if (debug)
        {
            fprintf(debug, "Total nr of excl on atom %d: %d\n", (i+1), nex);
        }
        snew(s, nex);

        nrs = 0;
        /* Again, starting with non-self exclusions */
        for (nre = 1; nre < nnb->nrex; nre++)
        {
            for (nrx = 0; nrx < nnb->nrexcl[i][nre]; nrx++)
            {
                s[nrs].ai = i;
                s[nrs].aj = nnb->a[i][nre][nrx];
                nrs++;
            }
        }

        /* sort */
        if (nrs != nex)
        {
            gmx_incons("Generating exclusions for Drudes");
        }
        if (nex > 1)
        {
            qsort ((void *)s, nex, (size_t)sizeof(s[0]), bond_sort);
        }

        /* remove duplicates */
        j_index = 0;
        if (nex > 0)
        {
            for (j = 1; j < nex; j++)
            {
                if ((s[j].ai != s[j-1].ai) || (s[j].aj != s[j-1].aj))
                {
                    s[j_index++] = s[j-1];
                }
            }
            s[j_index++] = s[j-1];
        }
        nex = j_index;

        /* resize after removal of duplicates */
        excls[i].nr = nex;
        srenew(excls[i].e, nex);
        /* put the cleaned exclusions into the array */
        for (nrs = 0; nrs < nex; nrs++)
        {
            excls[i].e[nrs] = s[nrs].aj;
        }
        sfree(s);
    }

    /* Now, we are ready to generate the Drude and LP exclusions, which are
     * the same as those of the parent atoms */
    for (i = 0; i < atoms->nr; i++)
    {
        /* loop over the exclusions for each atom */
        for (j = 0; j < excls[i].nr; j++)
        {
            i1 = i;
            i2 = excls[i].e[j];

            if (i1 > i2)
            {
                itmp = i1;
                i1   = i2;
                i2   = itmp;
            }
            /* H - H exclusions are already in excls[], so we just need to deal with
             * any heavy atoms here */
            if ((is_real_atom(atoms, i1) && is_real_atom(atoms, i2)) &&
                !(is_hydro(atoms, i1) && is_hydro(atoms, i2)))
            {
                gen_drude_lp_excl(plist, atoms, excls, i1, i2);
            }
        }
    }

    /* sort and remove duplicates now that Drude and LP exclusions have been built */
    for (i = 0; i < atoms->nr; i++)
    {           
        nex = 0;
        nrs = 0;
        if (excls[i].nr > 1)
        {
            nex = excls[i].nr;
            qsort(excls[i].e, excls[i].nr, (size_t)sizeof(int), int_comp);

            /* remove duplicates, as above */
            snew(s, nex);
            for (j = 0; j < nex; j++)
            {
                s[j].ai = i;
                s[j].aj = excls[i].e[j];
                nrs++;
            }
            if (nrs != nex)
            {
                gmx_incons("Sorting and cleaning Drude exclusions");
            }
            j_index = 0;
            if (nex > 0)
            {
                for (j = 1; j < nex; j++)
                {
                    if ((s[j].ai != s[j-1].ai) || (s[j].aj != s[j-1].aj))
                    {
                        s[j_index++] = s[j-1];
                    }
                }
                s[j_index++] = s[j-1];
            }
            nex = j_index;
            /* resize excls and finish */
            excls[i].nr = nex;
            srenew(excls[i].e, nex);
            for (nrs = 0; nrs < nex; nrs++)
            {
                excls[i].e[nrs] = s[nrs].aj;
            }
            sfree(s);
        }
    }  
}

static void gen_excls(t_atoms *atoms, t_excls *excls, t_hackblock hb[],
                      gmx_bool bAllowMissing, gmx_bool bDrude)
{

    int         r;
    int         a, astart, i1, i2, itmp;
    t_rbondeds *hbexcl;
    int         e;
    char       *anm;
    const char *ptr;

    /* need to allow for missing atoms due to the complexity of the bonded
     * interactions specified in both the .rtp and .tdb files */
    if (bDrude)
    {
        bAllowMissing = TRUE;
    }

    if (debug)
    {
        ptr = "exclusions";
    }
    else
    {
        ptr = "check";
    }

    astart = 0;
    for (a = 0; a < atoms->nr; a++)
    {
        r = atoms->atom[a].resind;
        if (a == atoms->nr-1 || atoms->atom[a+1].resind != r)
        {
            hbexcl = &hb[r].rb[ebtsEXCLS];

            for (e = 0; e < hbexcl->nb; e++)
            {
                anm = hbexcl->b[e].a[0];
                i1  = search_atom(anm, astart, atoms,
                                  ptr, bAllowMissing);
                anm = hbexcl->b[e].a[1];
                i2  = search_atom(anm, astart, atoms,
                                  "exclusion", bAllowMissing);
                if (i1 != -1 && i2 != -1)
                {
                    if (i1 > i2)
                    {
                        itmp = i1;
                        i1   = i2;
                        i2   = itmp;
                    }
                    /* Consider only atoms here, do Drudes and LP below in a separate function - cleaner! */
                    if (is_real_atom(atoms, i1) && is_real_atom(atoms, i2))
                    {
                        srenew(excls[i1].e, excls[i1].nr+1);
                        excls[i1].e[excls[i1].nr] = i2;
                        excls[i1].nr++;
                    }
                }
            }

            astart = a+1;
        }
    }

    for (a = 0; a < atoms->nr; a++)
    {
        if (excls[a].nr > 1)
        {
            qsort(excls[a].e, excls[a].nr, (size_t)sizeof(int), int_comp);
        }
    }

     if (debug)
    {
        fprintf(debug, "At end of gen_excl:\n");
        for (a = 0; a < atoms->nr; a++)
        {
            if (excls[a].nr > 1)
            {
                int q;
                fprintf(debug, "excluded from %d: ", a+1);
                for (q = 0; q < excls[a].nr; q++)
                {
                    fprintf(debug, "%5d", excls[a].e[q]+1);
                }
                fprintf(debug, "\n");
            }
        }
    }
}

static void remove_excl(t_excls *excls, int remove)
{
    int i;

    for (i = remove+1; i < excls->nr; i++)
    {
        excls->e[i-1] = excls->e[i];
    }

    excls->nr--;
}

void clean_excls(t_nextnb *nnb, int nrexcl, t_excls excls[])
{
    int      i, j, j1, k, k1, l, l1, e;
    t_excls *excl;

    if (nrexcl >= 1)
    {
        /* extract all i-j-k-l neighbours from nnb struct */
        for (i = 0; (i < nnb->nr); i++)
        {
            /* For all particles */
            excl = &excls[i];

            for (j = 0; (j < nnb->nrexcl[i][1]); j++)
            {
                /* For all first neighbours */
                j1 = nnb->a[i][1][j];

                for (e = 0; e < excl->nr; e++)
                {
                    if (excl->e[e] == j1)
                    {
                        remove_excl(excl, e);
                    }
                }

                if (nrexcl >= 2)
                {
                    for (k = 0; (k < nnb->nrexcl[j1][1]); k++)
                    {
                        /* For all first neighbours of j1 */
                        k1 = nnb->a[j1][1][k];

                        for (e = 0; e < excl->nr; e++)
                        {
                            if (excl->e[e] == k1)
                            {
                                remove_excl(excl, e);
                            }
                        }

                        if (nrexcl >= 3)
                        {
                            for (l = 0; (l < nnb->nrexcl[k1][1]); l++)
                            {
                                /* For all first neighbours of k1 */
                                l1 = nnb->a[k1][1][l];

                                for (e = 0; e < excl->nr; e++)
                                {
                                    if (excl->e[e] == l1)
                                    {
                                        remove_excl(excl, e);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void generate_excls(t_nextnb *nnb, int nrexcl, t_excls excls[])
{
    int      i, j, n, N;
    t_excls *excl;

    for (N = 1; (N < std::min(nrexcl, nnb->nrex)); N++)
    {
        /* extract all i-j-k-l neighbours from nnb struct */
        for (i = 0; (i < nnb->nr); i++)
        {
            /* For all particles */
            excl      = &excls[i];
            n         = excl->nr;
            excl->nr += nnb->nrexcl[i][N];
            srenew(excl->e, excl->nr);
            for (j = 0; (j < nnb->nrexcl[i][N]); j++)
            {
                /* For all first neighbours */
                if (nnb->a[i][N][j] != i)
                {
                    excl->e[n++] = nnb->a[i][N][j];
                }
            }
        }
    }
}

/* Generate pairs for Drudes and LP */
static void gen_drude_lp_pairs(t_params plist[], t_atoms *atoms, t_param *pai, int *npai, int ai, int aj)
{
    int i, j, f;

    if (debug)
    {
        fprintf(debug, "Generating D/LP excl for %s-%s\n", *(atoms->atomname[ai]), *(atoms->atomname[aj]));
    }

    /* The two atoms passed to the function are real atoms, so we only need
     * to build any pair that actually involves a Drude or LP */

    /* Always check against atoms->nr to avoid seg faults */

    if ((aj+1) < atoms->nr)
    {
        /* Polarizable atom found */
        if (is_d(atoms, (aj+1)))
        {
            if (debug)
            {
                fprintf(debug, "  gen pair: %s-%s\n", *(atoms->atomname[ai]), *(atoms->atomname[aj+1]));
            }
            /* atom - Drude */
            pai[(*npai)].ai() = ai;
            pai[(*npai)].aj() = aj+1;
            pai[(*npai)].c0() = NOTSET;
            pai[(*npai)].c1() = NOTSET;
            set_p_string(&(pai[(*npai)]), "");
            (*npai)++;
            /* Drude - Drude */
            if ((ai+1) < atoms->nr)
            {
                if (is_d(atoms, (ai+1)))
                {
                    if (debug)
                    {
                        fprintf(debug, "  gen pair: %s-%s\n", *(atoms->atomname[ai+1]), *(atoms->atomname[aj+1]));
                    }
                    pai[(*npai)].ai() = ai+1;
                    pai[(*npai)].aj() = aj+1;
                    pai[(*npai)].c0() = NOTSET;
                    pai[(*npai)].c1() = NOTSET;
                    set_p_string(&(pai[(*npai)]), "");
                    (*npai)++;
                }
            }
            /* loop over all possible vsite functions to add LP pairs */
            for (f = F_VSITE2; f <= F_VSITE4FDN; f++)
            {
                if (debug)
                {
                    fprintf(debug, " Found %d LP in plist\n", plist[f].nr);
                }
                for (i = 0; i < plist[f].nr; i++)
                {
                    /* atom aj is a host atom of at least one LP */
                    if (aj == plist[f].param[i].aj())
                    {
                        if (debug)
                        {
                            fprintf(debug, "  gen pair: %s-%s\n", *(atoms->atomname[ai]), *(atoms->atomname[plist[f].param[i].ai()]));
                        }
                        /* atom - LP */
                        pai[(*npai)].ai() = ai;
                        pai[(*npai)].aj() = plist[f].param[i].ai();
                        pai[(*npai)].c0() = NOTSET;
                        pai[(*npai)].c1() = NOTSET;
                        set_p_string(&(pai[(*npai)]), "");
                        (*npai)++;
                        /* Drude - LP */
                        if ((ai+1) < atoms->nr)
                        {
                            if (is_d(atoms, (ai+1)))
                            {
                                if (debug)
                                {
                                    fprintf(debug, "  gen pair: %s-%s\n", *(atoms->atomname[ai+1]), *(atoms->atomname[plist[f].param[i].ai()]));
                                }
                                pai[(*npai)].ai() = ai+1;
                                pai[(*npai)].aj() = plist[f].param[i].ai();
                                pai[(*npai)].c0() = NOTSET;
                                pai[(*npai)].c1() = NOTSET;
                                set_p_string(&(pai[(*npai)]), "");
                                (*npai)++;
                            }
                        }
                        /* LP - LP */
                        /* Unlike with exclusions, we only need to set pairs for interatomic LP */
                        for (j = (i+1); j < plist[f].nr; j++)
                        {
                            if (ai == plist[f].param[j].aj())
                            {
                                if (debug)
                                {
                                    fprintf(debug, "  gen pair: %s-%s\n", *(atoms->atomname[plist[f].param[i].ai()]), *(atoms->atomname[plist[f].param[j].ai()]));
                                }
                                pai[(*npai)].ai() = plist[f].param[i].ai();
                                pai[(*npai)].aj() = plist[f].param[j].ai();
                                pai[(*npai)].c0() = NOTSET;
                                pai[(*npai)].c1() = NOTSET;
                                set_p_string(&(pai[(*npai)]), "");
                                (*npai)++;
                            }
                        }
                    }
                }
            }   /* end of i-loop */
        }
    }

    if ((ai+1) < atoms->nr)
    {
        /* Polarizable atom found */
        if (is_d(atoms, (ai+1)))
        {
            if (debug)
            {
                fprintf(debug, "  gen pair: %s-%s\n", *(atoms->atomname[aj]), *(atoms->atomname[ai+1]));
            }
            /* atom - Drude */
            /* NOTE: we do not need to do Drude-Drude here because it is redundant with above */
            pai[(*npai)].ai() = aj;
            pai[(*npai)].aj() = ai+1;
            pai[(*npai)].c0() = NOTSET;
            pai[(*npai)].c1() = NOTSET;
            set_p_string(&(pai[(*npai)]), "");
            (*npai)++;
            /* loop over all possible vsite functions to add LP exclusions */
            for (f = F_VSITE2; f <= F_VSITE4FDN; f++)
            {
                for (i = 0; i < plist[f].nr; i++)
                {
                    /* atom ai is a host atom of at least one LP */
                    if (ai == plist[f].param[i].aj())
                    {
                        if (debug)
                        {
                            fprintf(debug, "  gen pair: %s-%s\n", *(atoms->atomname[aj]), *(atoms->atomname[plist[f].param[i].ai()]));
                        }
                        /* atom - LP */
                        pai[(*npai)].ai() = aj;
                        pai[(*npai)].aj() = plist[f].param[i].ai();
                        pai[(*npai)].c0() = NOTSET;
                        pai[(*npai)].c1() = NOTSET;
                        set_p_string(&(pai[(*npai)]), "");
                        (*npai)++;
                        /* Drude - LP */
                        if ((aj+1) < atoms->nr)
                        {
                            if (is_d(atoms, (aj+1)))
                            {
                                if (debug)
                                {
                                    fprintf(debug, "  gen pair: %s-%s\n", *(atoms->atomname[aj+1]), *(atoms->atomname[plist[f].param[i].ai()]));
                                }
                                pai[(*npai)].ai() = aj+1;
                                pai[(*npai)].aj() = plist[f].param[i].ai();
                                pai[(*npai)].c0() = NOTSET;
                                pai[(*npai)].c1() = NOTSET;
                                set_p_string(&(pai[(*npai)]), "");
                                (*npai)++;
                            }
                        }
                        /* LP - LP */
                        /* Unlike with exclusions, we only need to set pairs for interatomic LP */
                        for (j = (i+1); j < plist[f].nr; j++)
                        {
                            if ((aj == plist[f].param[j].aj()))
                            {
                                if (debug)
                                {
                                    fprintf(debug, "  gen pair: %s-%s\n", *(atoms->atomname[plist[f].param[i].ai()]), *(atoms->atomname[plist[f].param[j].ai()]));
                                }
                                pai[(*npai)].ai() = plist[f].param[i].ai();
                                pai[(*npai)].aj() = plist[f].param[j].ai();
                                pai[(*npai)].c0() = NOTSET;
                                pai[(*npai)].c1() = NOTSET;
                                set_p_string(&(pai[(*npai)]), "");
                                (*npai)++;
                            }
                        }
                    }
                }
            }   /* end of i-loop */
        }
    }
}

/* Generate pairs, angles and dihedrals from .rtp settings */
void gen_pad(t_nextnb *nnb, t_atoms *atoms, t_restp rtp[],
             t_params plist[], t_excls excls[], t_hackblock hb[],
             gmx_bool bAllowMissing, gmx_bool bDrude)
{
    t_param    *ang, *dih, *pai, *improper;
    t_param    *thole, *aniso, *pol, *vsites;   /* these are only needed with Drude FF */
    t_rbondeds *hbang, *hbdih;
    char      **anm;
    const char *p;
    char       *ts;     /* For Thole parameters */
    int         res, minres, maxres;
    int         i, j, j1, k, k1, l, l1, m, n, q, q1, i1, i2;
    int         di, dj1, dk1;
    int         ninc, maxang, maxdih, maxpai, maxthole;
    int         nang, ndih, npai, nimproper, nbd;
    int         nthole, naniso, npol, nvsites;
    int         nFound;
    gmx_bool    bFound, bExcl;

    /* These are the angles, dihedrals and pairs that we generate
     * from the bonds. The ones that are already there from the rtp file
     * will be retained.
     */
    nang   = 0;
    npai   = 0;
    ndih   = 0;
    ninc   = 500;
    maxang = maxdih = maxpai = maxthole = ninc;
    snew(ang, maxang);
    snew(dih, maxdih);
    snew(pai, maxpai);

    snew(thole, ninc);
    snew(ts, STRLEN);
    snew(aniso, ninc);
    snew(pol, ninc);
    snew(vsites, ninc);

    if (bDrude)
    {
        /* Must be populated here because we need plist to generate pairs.
         * Call a generalized routine here to get bondeds from hackblocks
         * and add them to plist. Thole will be generated below. */
        fprintf(stderr, "Generating anisotropic polarization...");
        naniso = get_tdb_bonded(atoms, hb, &aniso, F_ANISO_POL);
        fprintf(stderr, "wrote %d entries.\n", naniso);

        fprintf(stderr, "Generating isotropic polarization...");
        npol = get_tdb_bonded(atoms, hb, &pol, F_POLARIZATION);
        fprintf(stderr, "wrote %d entries.\n", npol);

        fprintf(stderr, "Generating virtual sites from lone pairs...");
        nvsites = get_tdb_bonded(atoms, hb, &vsites, F_VSITE3);
        fprintf(stderr, "wrote %d virtual sites.\n", nvsites);

        cppar(aniso, naniso, plist, F_ANISO_POL);
        cppar(pol, npol, plist, F_POLARIZATION);
        cppar(vsites, nvsites, plist, F_VSITE3);

        nthole = 0;
    }

    snew(anm, 4);
    for (i = 0; i < 4; i++)
    {
        snew(anm[i], 12);
    }

    if (hb)
    {
        gen_excls(atoms, excls, hb, bAllowMissing, bDrude);
        /* mark all entries as not matched yet */
        for (i = 0; i < atoms->nres; i++)
        {
            for (j = 0; j < ebtsNR; j++)
            {
                for (k = 0; k < hb[i].rb[j].nb; k++)
                {
                    hb[i].rb[j].b[k].match = FALSE;
                }
            }
        }
    }

    /* Extract all i-j-k-l neighbours from nnb struct to generate all
     * angles and dihedrals. */
    for (i = 0; (i < nnb->nr); i++)
    {
        /* For all particles */
        for (j = 0; (j < nnb->nrexcl[i][1]); j++)
        {
            /* For all first neighbours */
            j1 = nnb->a[i][1][j];

            /* 1-2 Thole interactions for bonded atoms */
            if (bDrude)
            {
                di  = NOTSET;
                dj1 = NOTSET;

                /* Add Thole pair if we find two polarizable atoms */
                if ((atoms->atom[i].alpha != 0) && (atoms->atom[j1].alpha != 0))
                {
                    if (nthole == maxthole)
                    {
                        maxthole += ninc;
                        srenew(thole, maxthole);
                    }
                    /* Find the Drudes associated with i and j1, these will be within the
                     * first neighbors of both atoms */
                    for (q = 0; (q < nnb->nrexcl[i][1]); q++)
                    {
                        q1 = nnb->a[i][1][q];
                        if (is_d(atoms, q1))
                        {
                            di = q1;
                        }
                    }
                    for (q = 0; (q < nnb->nrexcl[j1][1]); q++)
                    {
                        q1 = nnb->a[j1][1][q];
                        if (is_d(atoms, q1))
                        {
                            dj1 = q1;
                        }
                    }
                    thole[nthole].ai() = i;     /* atom */
                    thole[nthole].aj() = di;    /* Drude */
                    thole[nthole].ak() = j1;    /* atom */
                    thole[nthole].al() = dj1;   /* Drude */
                    /* protect against weirdness: if we did not find a Drude but we 
                     * somehow detected a polarizable atom, there's a problem */
                    if ((di == NOTSET) || (dj1 == NOTSET))
                    {
                        gmx_fatal(FARGS, "Could not find Drude when adding 1-2 Thole: %s-%s\n",
                                    *(atoms->atomname[i]), *(atoms->atomname[j1]));
                    }
                    thole[nthole].c0() = NOTSET;
                    thole[nthole].c1() = NOTSET;
                    sprintf(ts, "%10.6f %10.6f %8.4f %8.4f", atoms->atom[i].alpha, atoms->atom[j1].alpha,
                            atoms->atom[i].thole, atoms->atom[j1].thole);
                    set_p_string(&(thole[nthole]), ts);
                    nthole++;
                }
            }
            for (k = 0; (k < nnb->nrexcl[j1][1]); k++)
            {
                /* For all first neighbours of j1 */
                k1 = nnb->a[j1][1][k];
                if (k1 != i)
                {
                    /* Generate every angle only once */
                    if (i < k1)
                    {
                        if (nang == maxang)
                        {
                            maxang += ninc;
                            srenew(ang, maxang);
                        }
                        ang[nang].ai() = i;
                        ang[nang].aj() = j1;
                        ang[nang].ak() = k1;
                        ang[nang].c0() = NOTSET;
                        ang[nang].c1() = NOTSET;
                        set_p_string(&(ang[nang]), "");

                        /* 1-3 Thole interactions for atoms in angle */
                        if (bDrude)
                        {
                            di  = NOTSET;
                            dk1 = NOTSET;

                            /* Add Thole pair if we find two polarizable atoms */
                            if ((atoms->atom[i].alpha != 0) && (atoms->atom[k1].alpha != 0))
                            {
                                if (nthole == maxthole)
                                {
                                    maxthole += ninc;
                                    srenew(thole, maxthole);
                                }
                                /* Find the Drudes associated with i and k1, these will be within the
                                 * first neighbors of both atoms */
                                for (q = 0; (q < nnb->nrexcl[i][1]); q++)
                                {
                                    q1 = nnb->a[i][1][q];
                                    if (is_d(atoms, q1))
                                    {
                                        di = q1;
                                    }
                                }
                                for (q = 0; (q < nnb->nrexcl[k1][1]); q++)
                                {
                                    q1 = nnb->a[k1][1][q];
                                    if (is_d(atoms, q1))
                                    {
                                        dk1 = q1;
                                    }
                                }
                                thole[nthole].ai() = i;     /* atom */
                                thole[nthole].aj() = di;    /* Drude */
                                thole[nthole].ak() = k1;    /* atom */
                                thole[nthole].al() = dk1;   /* Drude */
                                /* as above */
                                if ((di == NOTSET) || (dk1 == NOTSET))
                                {
                                    gmx_fatal(FARGS, "Could not find Drude when adding 1-3 Thole: %s-%s\n",
                                                *(atoms->atomname[i]), *(atoms->atomname[k1]));
                                }
                                thole[nthole].c0() = NOTSET;
                                thole[nthole].c1() = NOTSET;
                                sprintf(ts, "%10.6f %10.6f %8.4f %8.4f", atoms->atom[i].alpha, atoms->atom[k1].alpha,
                                        atoms->atom[i].thole, atoms->atom[k1].thole);
                                set_p_string(&(thole[nthole]), ts);
                                nthole++;
                            }
                        }

                        if (hb)
                        {
                            minres = atoms->atom[ang[nang].a[0]].resind;
                            maxres = minres;
                            for (m = 1; m < 3; m++)
                            {
                                minres = std::min(minres, atoms->atom[ang[nang].a[m]].resind);
                                maxres = std::max(maxres, atoms->atom[ang[nang].a[m]].resind);
                            }
                            res = 2*minres-maxres;
                            do
                            {
                                res += maxres-minres;
                                get_atomnames_min(3, anm, res, atoms, ang[nang].a);
                                hbang = &hb[res].rb[ebtsANGLES];
                                for (l = 0; (l < hbang->nb); l++)
                                {
                                    if (strcmp(anm[1], hbang->b[l].aj()) == 0)
                                    {
                                        bFound = FALSE;
                                        for (m = 0; m < 3; m += 2)
                                        {
                                            bFound = (bFound ||
                                                      ((strcmp(anm[m], hbang->b[l].ai()) == 0) &&
                                                       (strcmp(anm[2-m], hbang->b[l].ak()) == 0)));
                                        }
                                        if (bFound)
                                        {
                                            set_p_string(&(ang[nang]), hbang->b[l].s);
                                            /* Mark that we found a match for this entry */
                                            hbang->b[l].match = TRUE;
                                        }
                                    }
                                }
                            }
                            while (res < maxres);
                        }
                        nang++;
                    }
                    /* Generate every dihedral, 1-4 exclusion and 1-4 interaction
                       only once */
                    if (j1 < k1)
                    {
                        for (l = 0; (l < nnb->nrexcl[k1][1]); l++)
                        {
                            /* For all first neighbours of k1 */
                            l1 = nnb->a[k1][1][l];
                            if ((l1 != i) && (l1 != j1))
                            {
                                if (ndih == maxdih)
                                {
                                    maxdih += ninc;
                                    srenew(dih, maxdih);
                                }
                                dih[ndih].ai() = i;
                                dih[ndih].aj() = j1;
                                dih[ndih].ak() = k1;
                                dih[ndih].al() = l1;
                                for (m = 0; m < MAXFORCEPARAM; m++)
                                {
                                    dih[ndih].c[m] = NOTSET;
                                }
                                set_p_string(&(dih[ndih]), "");
                                nFound = 0;
                                if (hb)
                                {
                                    minres = atoms->atom[dih[ndih].a[0]].resind;
                                    maxres = minres;
                                    for (m = 1; m < 4; m++)
                                    {
                                        minres = std::min(minres, atoms->atom[dih[ndih].a[m]].resind);
                                        maxres = std::max(maxres, atoms->atom[dih[ndih].a[m]].resind);
                                    }
                                    res = 2*minres-maxres;
                                    do
                                    {
                                        res += maxres-minres;
                                        get_atomnames_min(4, anm, res, atoms, dih[ndih].a);
                                        hbdih = &hb[res].rb[ebtsPDIHS];
                                        for (n = 0; (n < hbdih->nb); n++)
                                        {
                                            bFound = FALSE;
                                            for (m = 0; m < 2; m++)
                                            {
                                                bFound = (bFound ||
                                                          ((strcmp(anm[3*m],  hbdih->b[n].ai()) == 0) &&
                                                           (strcmp(anm[1+m],  hbdih->b[n].aj()) == 0) &&
                                                           (strcmp(anm[2-m],  hbdih->b[n].ak()) == 0) &&
                                                           (strcmp(anm[3-3*m], hbdih->b[n].al()) == 0)));
                                            }
                                            if (bFound)
                                            {
                                                set_p_string(&dih[ndih], hbdih->b[n].s);
                                                /* Mark that we found a match for this entry */
                                                hbdih->b[n].match = TRUE;

                                                /* Set the last parameter to be able to see
                                                   if the dihedral was in the rtp list.
                                                 */
                                                dih[ndih].c[MAXFORCEPARAM-1] = DIHEDRAL_WAS_SET_IN_RTP;
                                                nFound++;
                                                ndih++;
                                                /* Set the next direct in case the rtp contains
                                                   multiple entries for this dihedral.
                                                 */
                                                if (ndih == maxdih)
                                                {
                                                    maxdih += ninc;
                                                    srenew(dih, maxdih);
                                                }
                                                dih[ndih].ai() = i;
                                                dih[ndih].aj() = j1;
                                                dih[ndih].ak() = k1;
                                                dih[ndih].al() = l1;
                                                for (m = 0; m < MAXFORCEPARAM; m++)
                                                {
                                                    dih[ndih].c[m] = NOTSET;
                                                }
                                            }
                                        }
                                    }
                                    while (res < maxres);
                                }
                                if (nFound == 0)
                                {
                                    if (ndih == maxdih)
                                    {
                                        maxdih += ninc;
                                        srenew(dih, maxdih);
                                    }
                                    dih[ndih].ai() = i;
                                    dih[ndih].aj() = j1;
                                    dih[ndih].ak() = k1;
                                    dih[ndih].al() = l1;
                                    for (m = 0; m < MAXFORCEPARAM; m++)
                                    {
                                        dih[ndih].c[m] = NOTSET;
                                    }
                                    set_p_string(&(dih[ndih]), "");
                                    ndih++;
                                }

                                nbd = nb_dist(nnb, i, l1);
                                if (debug)
                                {
                                    fprintf(debug, "Distance (%d-%d) = %d\n", i+1, l1+1, nbd);
                                }
                                if (nbd == 3)
                                {
                                    i1    = std::min(i, l1);
                                    i2    = std::max(i, l1);
                                    bExcl = FALSE;
                                    for (m = 0; m < excls[i1].nr; m++)
                                    {
                                        bExcl = bExcl || excls[i1].e[m] == i2;
                                    }
                                    if (!bExcl)
                                    {
                                        if (rtp[0].bGenerateHH14Interactions ||
                                            !(is_hydro(atoms, i1) && is_hydro(atoms, i2)))
                                        {
                                            if (npai == maxpai)
                                            {
                                                maxpai += ninc;
                                                srenew(pai, maxpai);
                                            }
                                            /* Prevent Drudes and LP being counted in 1-4. We deal with those separately below
                                             * and only in the case of a polarizable system (e.g. bDrude == TRUE) */
                                            if (is_real_atom(atoms, i1) && is_real_atom(atoms, i2))
                                            {
                                                if (debug)
                                                {
                                                    fprintf(debug, "In gen_pad: found real atoms %d and %d for pair.\n", (i1+1), (i2+1));
                                                }
                                                pai[npai].ai() = i1;
                                                pai[npai].aj() = i2;
                                                pai[npai].c0() = NOTSET;
                                                pai[npai].c1() = NOTSET;
                                                set_p_string(&(pai[npai]), "");
                                                npai++;

                                                /* Drudes and LP have the same pairs as the parent atom */
                                                if (bDrude)
                                                {
                                                    /* The largest number of pairs that can be added for any atom-atom combination 
                                                     * is 9, so increase allocation before even entering the function. This is a bit
                                                     * of a hack, but calling srenew() within gen_drude_lp_pairs() did not always work
                                                     * with different versions of gcc, the reason for which was not clear to me. */
                                                    if (npai >= (maxpai-10))
                                                    {
                                                        maxpai += ninc;
                                                        srenew(pai, maxpai);
                                                    }
                                                    gen_drude_lp_pairs(plist, atoms, pai, &npai, i1, i2);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* At this point, the Thole pairs are generated, but this is a convenient
     * place to put the output */
    if (bDrude)
    {
        fprintf(stderr, "Generating Thole pairs...wrote %d pairs.\n", nthole);
        /* ...and actually save the parameters */
        cppar(thole, nthole, plist, F_THOLE_POL);
    }

    if (hb)
    {
        /* The above approach is great in that we double-check that e.g. an angle
         * really corresponds to three atoms connected by bonds, but this is not
         * generally true. Go through the angle and dihedral hackblocks to add
         * entries that we have not yet marked as matched when going through bonds.
         */
        for (i = 0; i < atoms->nres; i++)
        {
            /* Add remaining angles from hackblock */
            hbang = &hb[i].rb[ebtsANGLES];
            for (j = 0; j < hbang->nb; j++)
            {
                if (hbang->b[j].match == TRUE)
                {
                    /* We already used this entry, continue to the next */
                    continue;
                }
                /* Hm - entry not used, let's see if we can find all atoms */
                if (nang == maxang)
                {
                    maxang += ninc;
                    srenew(ang, maxang);
                }
                bFound = TRUE;
                for (k = 0; k < 3 && bFound; k++)
                {
                    p   = hbang->b[j].a[k];
                    res = i;
                    if (p[0] == '-')
                    {
                        p++;
                        res--;
                    }
                    else if (p[0] == '+')
                    {
                        p++;
                        res++;
                    }
                    ang[nang].a[k] = search_res_atom(p, res, atoms, "angle", TRUE);
                    bFound         = (ang[nang].a[k] != -1);
                }
                ang[nang].c0() = NOTSET;
                ang[nang].c1() = NOTSET;

                if (bFound)
                {
                    set_p_string(&(ang[nang]), hbang->b[j].s);
                    hbang->b[j].match = TRUE;
                    /* Incrementing nang means we save this angle */
                    nang++;
                }
            }

            /* Add remaining dihedrals from hackblock */
            hbdih = &hb[i].rb[ebtsPDIHS];
            for (j = 0; j < hbdih->nb; j++)
            {
                if (hbdih->b[j].match == TRUE)
                {
                    /* We already used this entry, continue to the next */
                    continue;
                }
                /* Hm - entry not used, let's see if we can find all atoms */
                if (ndih == maxdih)
                {
                    maxdih += ninc;
                    srenew(dih, maxdih);
                }
                bFound = TRUE;
                for (k = 0; k < 4 && bFound; k++)
                {
                    p   = hbdih->b[j].a[k];
                    res = i;
                    if (p[0] == '-')
                    {
                        p++;
                        res--;
                    }
                    else if (p[0] == '+')
                    {
                        p++;
                        res++;
                    }
                    dih[ndih].a[k] = search_res_atom(p, res, atoms, "dihedral", TRUE);
                    bFound         = (dih[ndih].a[k] != -1);
                }
                for (m = 0; m < MAXFORCEPARAM; m++)
                {
                    dih[ndih].c[m] = NOTSET;
                }

                if (bFound)
                {
                    set_p_string(&(dih[ndih]), hbdih->b[j].s);
                    hbdih->b[j].match = TRUE;
                    /* Incrementing ndih means we save this dihedral */
                    ndih++;
                }
            }
        }
    }

    /* Sort angles with respect to j-i-k (middle atom first) */
    if (nang > 1)
    {
        qsort(ang, nang, (size_t)sizeof(ang[0]), acomp);
    }

    /* Sort dihedrals with respect to j-k-i-l (middle atoms first) */
    if (ndih > 1)
    {
        qsort(dih, ndih, (size_t)sizeof(dih[0]), dcomp);
    }

    /* Sort the pairs */
    if (npai > 1)
    {
        qsort(pai, npai, (size_t)sizeof(pai[0]), pcomp);
    }
    if (npai > 0)
    {
        /* Remove doubles, could occur in 6-rings, such as phenyls,
           maybe one does not want this when fudgeQQ < 1.
         */
        fprintf(stderr, "Before cleaning: %d pairs\n", npai);
        rm2par(pai, &npai, preq);
    }

    /* Get the impropers from the database */
    nimproper = get_impropers(atoms, hb, &improper, bAllowMissing);

    /* Sort the impropers */
    sort_id(nimproper, improper);

    if (ndih > 0)
    {
        fprintf(stderr, "Before cleaning: %d dihedrals\n", ndih);
        clean_dih(dih, &ndih, improper, nimproper, atoms,
                  rtp[0].bKeepAllGeneratedDihedrals,
                  rtp[0].bRemoveDihedralIfWithImproper);
    }

    /* clean angles - used only for Drude FF */
    if (nang > 1 && bDrude)
    {
        fprintf(stderr, "Before cleaning: %d angles\n", nang);
        clean_ang(ang, &nang, atoms);
    }

    /* Now we have unique lists of angles and dihedrals
     * Copy them into the destination struct
     */
    cppar(ang, nang, plist, F_ANGLES);
    cppar(dih, ndih, plist, F_PDIHS);
    cppar(improper, nimproper, plist, F_IDIHS);
    cppar(pai, npai, plist, F_LJ14);

    /* Remove all exclusions which are within nrexcl */
    /* Leave all assigned exclusions in case of Drude? TESTING */
    if (!bDrude)
    {
        clean_excls(nnb, rtp[0].nrexcl, excls);
    }

    sfree(ang);
    sfree(dih);
    sfree(improper);
    sfree(pai);

    if (bDrude)
    {
        sfree(thole);
        sfree(aniso);
        sfree(pol);
        sfree(vsites);
    }
}
