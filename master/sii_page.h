/*****************************************************************************
 *
 *  Copyright (C) 2026  Florian Pose, Ingenieurgemeinschaft IgH
 *
 *  This file is part of the IgH EtherCAT Master.
 *
 *  The IgH EtherCAT Master is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License version 2, as
 *  published by the Free Software Foundation.
 *
 *  The IgH EtherCAT Master is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 *  Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the IgH EtherCAT Master; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 ****************************************************************************/

/**
   \file
   EtherCAT SII/Eeprom memory contents.
*/

/****************************************************************************/

#ifndef __EC_SII_PAGE_H__
#define __EC_SII_PAGE_H__

#include <linux/types.h>
#include <linux/list.h>

/****************************************************************************/

typedef enum {
    EC_SII_PAGE_UNKNOWN, /**< Information not available. */
    EC_SII_PAGE_FETCHED, /**< Completely read via SII. */
    EC_SII_PAGE_CACHED, /**< Taken from cache. */
} ec_sii_page_origin_t;

/****************************************************************************/

/** Slave information interface memory page.
 */
typedef struct ec_sii_page
{
    struct list_head list; /**< List item (for cache). */

    // Identification for caching
    uint32_t vendor_id; /**< Vendor ID. */
    uint32_t product_code; /**< Vendor-specific product code. */
    uint32_t revision_number; /**< Revision number. */
    uint32_t serial_number; /**< Serial number. */
    uint16_t alias; /**< Configured station alias. */

    ec_sii_page_origin_t origin; /**< Page origin. */

    uint16_t *words; /**< Complete SII image. */
    size_t word_count; /**< Size of the SII contents in words. */
} ec_sii_page_t;

/****************************************************************************/

// slave construction/destruction
void ec_sii_page_init(ec_sii_page_t *);
void ec_sii_page_clear(ec_sii_page_t *);

int ec_sii_page_alloc(ec_sii_page_t *, size_t);
int ec_sii_page_copy(ec_sii_page_t *, const ec_sii_page_t *);

/****************************************************************************/

#endif
