/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2013,2014, by the GROMACS development team, led by
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

#ifndef GMX_FILEIO_XTCIO_H
#define GMX_FILEIO_XTCIO_H

#include "../math/vectypes.h"
#include "../utility/basedefinitions.h"
#include "../utility/real.h"
#include "gmxfio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* All functions return 1 if successful, 0 otherwise
 * bOK tells if a frame is not corrupted
 */

t_fileio *open_xtc(const char *filename, const char *mode);
/* Open a file for xdr I/O */

void close_xtc(t_fileio *fio);
/* Close the file for xdr I/O */

int read_first_xtc(t_fileio *fio,
                   int *natoms, int *step, real *time,
                   matrix box, rvec **x, real *prec, gmx_bool *bOK);
/* Open xtc file, read xtc file first time, allocate memory for x */

int read_next_xtc(t_fileio *fio,
                  int natoms, int *step, real *time,
                  matrix box, rvec *x, real *prec, gmx_bool *bOK);
/* Read subsequent frames */

int write_xtc(t_fileio *fio,
              int natoms, int step, real time,
              matrix box, rvec *x, real prec);
/* Write a frame to xtc file */

int xtc_check(const char *str, gmx_bool bResult, const char *file, int line);
#define XTC_CHECK(s, b) xtc_check(s, b, __FILE__, __LINE__)

void xtc_check_fat_err(const char *str, gmx_bool bResult, const char *file, int line);
#define XTC_CHECK_FAT_ERR(s, b) xtc_check_fat_err(s, b, __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif

#endif
