/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2012,2013,2014,2015,2017 The GROMACS development team.
 * Copyright (c) 2018,2019,2020, by the GROMACS development team, led by
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
#ifndef GMX_HARDWARE_GPU_HW_INFO_H
#define GMX_HARDWARE_GPU_HW_INFO_H

#include "gromacs/utility/basedefinitions.h"
#include "gromacs/utility/enumerationhelpers.h"

struct DeviceInformation;

//! Possible results of the GPU detection/check.
enum class DeviceStatus : int
{
    //! The device is compatible
    Compatible = 0,
    //! Device does not exist
    Nonexistent = 1,
    //! Device is not compatible
    Incompatible = 2,
    //! OpenCL device has incompatible cluster size for non-bonded kernels.
    IncompatibleClusterSize = 3,
    /*! \brief An error occurred he functionality checks.
     * That indicates malfunctioning of the device, driver, or incompatible driver/runtime.
     */
    NonFunctional = 4,
    /*! \brief CUDA devices are busy or unavailable.
     * typically due to use of \p cudaComputeModeExclusive, \p cudaComputeModeProhibited modes.
     */
    Unavailable = 5,
    //! Enumeration size
    Count = 6
};

/*! \brief Names of the GPU detection/check results
 *
 * Check-source wants to warn about the use of a symbol name that would
 * require an inclusion of config.h. However the use is in a comment, so that
 * is a false warning. So C-style string concatenation is used to fool the
 * naive parser in check-source. In turn, that needs a clang-format suppression
 * in order to look reasonable.
 */
static const gmx::EnumerationArray<DeviceStatus, const char*> c_deviceStateString = {
    "compatible", "nonexistent", "incompatible",
    // clang-format off
    "incompatible (please recompile with correct GMX" "_OPENCL_NB_CLUSTER_SIZE of 4)",
    // clang-format on
    "non-functional", "unavailable"
};

/*! \brief Information about GPU devices on this physical node.
 *
 * Includes either CUDA or OpenCL devices.  The gmx_hardware_detect
 * module initializes it.
 *
 * \todo Use a std::vector */
struct gmx_gpu_info_t
{
    //! Did we attempt GPU detection?
    gmx_bool bDetectGPUs;
    //! Total number of GPU devices detected on this physical node
    int n_dev;
    //! Information about each GPU device detected on this physical node
    DeviceInformation* deviceInfo;
    //! Number of GPU devices detected on this physical node that are compatible.
    int n_dev_compatible;
};

#endif
