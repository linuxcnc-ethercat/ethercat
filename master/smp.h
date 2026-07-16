/*****************************************************************************
 *
 *  Copyright (C) 2006-2026  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT master.
 *
 *  The file is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by the
 *  Free Software Foundation; version 2.1 of the License.
 *
 *  This file is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public
 *  License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this file. If not, see <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/

/** \file
 * Definitions of Kernel SMP macros.
 */

/****************************************************************************/

#ifndef MASTER_SMP_H_
#define MASTER_SMP_H_

#include <linux/version.h>

/****************************************************************************/

/* Define SMP macros on kernel versions where they did not exist yet.
 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 12, 47)

#define smp_store_release(p, v) \
do { \
    smp_mb(); \
    ACCESS_ONCE(*p) = (v); \
} while (0)

#define smp_load_acquire(p) \
({ \
    typeof(*p) ___p1 = ACCESS_ONCE(*p); \
    smp_mb(); \
    ___p1; \
})

#endif

/****************************************************************************/

#endif  // MASTER_SMP_H_
