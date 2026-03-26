/*****************************************************************************
 *
 *  Copyright (C) 2006-2026  Florian Pose, Ingenieurgemeinschaft IgH
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
   EtherCAT slave state machines.
*/

/****************************************************************************/

#include "globals.h"
#include "master.h"
#include "mailbox.h"
#include "slave_config.h"

#include "fsm_slave_scan.h"

/****************************************************************************/

// prototypes for private methods
int ec_fsm_slave_scan_running(const ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_enter_sii_size(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_enter_assign_sii(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_enter_sii_ident(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_enter_datalink(ec_fsm_slave_scan_t *);
#ifdef EC_REGALIAS
void ec_fsm_slave_scan_enter_regalias(ec_fsm_slave_scan_t *);
#endif
void ec_fsm_slave_scan_enter_preop(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_enter_pdos(ec_fsm_slave_scan_t *);

/****************************************************************************/

void ec_fsm_slave_scan_state_start(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_address(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_state(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_base(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_dc_cap(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_dc_times(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_datalink(ec_fsm_slave_scan_t *);
#ifdef EC_SII_ASSIGN
void ec_fsm_slave_scan_state_assign_sii(ec_fsm_slave_scan_t *);
#endif
void ec_fsm_slave_scan_state_sii_ident(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_sii_size(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_sii_data(ec_fsm_slave_scan_t *);
#ifdef EC_REGALIAS
void ec_fsm_slave_scan_state_regalias(ec_fsm_slave_scan_t *);
#endif
void ec_fsm_slave_scan_state_preop(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_sync(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_pdos(ec_fsm_slave_scan_t *);

void ec_fsm_slave_scan_state_end(ec_fsm_slave_scan_t *);
void ec_fsm_slave_scan_state_error(ec_fsm_slave_scan_t *);

/****************************************************************************/

/** Constructor.
 */
void ec_fsm_slave_scan_init(
        ec_fsm_slave_scan_t *fsm, /**< Slave scanning state machine. */
        ec_datagram_t *datagram, /**< Datagram to use. */
        ec_fsm_slave_config_t *fsm_slave_config, /**< Slave configuration
                                                  state machine to use. */
        ec_fsm_pdo_t *fsm_pdo /**< PDO configuration machine to use. */
        )
{
    fsm->slave = NULL;
    fsm->datagram = datagram;
    fsm->fsm_slave_config = fsm_slave_config;
    fsm->fsm_pdo = fsm_pdo;

    fsm->retries = 0;
    fsm->state = NULL;
    fsm->sii_offset = 0;

    // init sub state machines
    ec_fsm_sii_init(&fsm->fsm_sii, fsm->datagram);
}

/****************************************************************************/

/** Destructor.
 */
void ec_fsm_slave_scan_clear(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    // clear sub state machines
    ec_fsm_sii_clear(&fsm->fsm_sii);
}

/****************************************************************************/

/**
 * Start slave scan state machine.
 */

void ec_fsm_slave_scan_start(
        ec_fsm_slave_scan_t *fsm, /**< slave state machine */
        ec_slave_t *slave /**< slave to configure */
        )
{
    fsm->slave = slave;
    fsm->state = ec_fsm_slave_scan_state_start;
}

/****************************************************************************/

/**
   \return false, if state machine has terminated
*/

int ec_fsm_slave_scan_running(
        const ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    return fsm->state != ec_fsm_slave_scan_state_end
        && fsm->state != ec_fsm_slave_scan_state_error;
}

/****************************************************************************/

/**
   Executes the current state of the state machine.
   If the state machine's datagram is not sent or received yet, the execution
   of the state machine is delayed to the next cycle.
   \return false, if state machine has terminated
*/

int ec_fsm_slave_scan_exec(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    if (fsm->datagram->state == EC_DATAGRAM_SENT
        || fsm->datagram->state == EC_DATAGRAM_QUEUED) {
        // datagram was not sent or received yet.
        return ec_fsm_slave_scan_running(fsm);
    }

    fsm->state(fsm);
    return ec_fsm_slave_scan_running(fsm);
}

/****************************************************************************/

/**
   \return true, if the state machine terminated gracefully
*/

int ec_fsm_slave_scan_success(
        const ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    return fsm->state == ec_fsm_slave_scan_state_end;
}

/*****************************************************************************
 *  slave scan state machine
 ****************************************************************************/

/**
   Slave scan state: START.
   First state of the slave state machine. Writes the station address to the
   slave, according to its ring position.
*/

void ec_fsm_slave_scan_state_start(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    // write station address
    ec_datagram_apwr(fsm->datagram, fsm->slave->ring_position, 0x0010, 2);
    EC_WRITE_U16(fsm->datagram->data, fsm->slave->station_address);
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_slave_scan_state_address;
}

/****************************************************************************/

/**
   Slave scan state: ADDRESS.
*/

void ec_fsm_slave_scan_state_address(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(fsm->slave,
                "Failed to receive station address datagram: ");
        ec_datagram_print_state(datagram);
        return;
    }

    if (datagram->working_counter != 1) {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(fsm->slave, "Failed to write station address: ");
        ec_datagram_print_wc_error(datagram);
        return;
    }

    // Read AL state
    ec_datagram_fprd(datagram, fsm->slave->station_address, 0x0130, 2);
    ec_datagram_zero(datagram);
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_slave_scan_state_state;
}

/****************************************************************************/

/**
   Slave scan state: STATE.
*/

void ec_fsm_slave_scan_state_state(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to receive AL state datagram: ");
        ec_datagram_print_state(datagram);
        return;
    }

    if (datagram->working_counter != 1) {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to read AL state: ");
        ec_datagram_print_wc_error(datagram);
        return;
    }

    slave->current_state = EC_READ_U8(datagram->data);
    if (slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
        char state_str[EC_STATE_STRING_SIZE];
        ec_state_string(slave->current_state, state_str, 0);
        EC_SLAVE_WARN(slave, "Slave has state error bit set (%s)!\n",
                state_str);
    }

    // read base data
    ec_datagram_fprd(datagram, fsm->slave->station_address, 0x0000, 12);
    ec_datagram_zero(datagram);
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_slave_scan_state_base;
}

/****************************************************************************/

/** Slave scan state: BASE.
 */
void ec_fsm_slave_scan_state_base(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;
    u8 octet;
    int i;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to receive base data datagram: ");
        ec_datagram_print_state(datagram);
        return;
    }

    if (datagram->working_counter != 1) {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to read base data: ");
        ec_datagram_print_wc_error(datagram);
        return;
    }

    slave->base_type       = EC_READ_U8 (datagram->data);
    slave->base_revision   = EC_READ_U8 (datagram->data + 1);
    slave->base_build      = EC_READ_U16(datagram->data + 2);

    slave->base_fmmu_count = EC_READ_U8 (datagram->data + 4);
    if (slave->base_fmmu_count > EC_MAX_FMMUS) {
        EC_SLAVE_WARN(slave, "Slave has more FMMUs (%u) than the master can"
                " handle (%u).\n", slave->base_fmmu_count, EC_MAX_FMMUS);
        slave->base_fmmu_count = EC_MAX_FMMUS;
    }

    slave->base_sync_count = EC_READ_U8(datagram->data + 5);
    if (slave->base_sync_count > EC_MAX_SYNC_MANAGERS) {
        EC_SLAVE_WARN(slave, "Slave provides more sync managers (%u)"
                " than the master can handle (%u).\n",
                slave->base_sync_count, EC_MAX_SYNC_MANAGERS);
        slave->base_sync_count = EC_MAX_SYNC_MANAGERS;
    }

    octet = EC_READ_U8(datagram->data + 7);
    for (i = 0; i < EC_MAX_PORTS; i++) {
        slave->ports[i].desc = (octet >> (2 * i)) & 0x03;
    }

    octet = EC_READ_U8(datagram->data + 8);
    slave->base_fmmu_bit_operation = octet & 0x01;
    slave->base_dc_supported = (octet >> 2) & 0x01;
    slave->base_dc_range = ((octet >> 3) & 0x01) ? EC_DC_64 : EC_DC_32;

    if (slave->base_dc_supported) {
        // read DC capabilities
        ec_datagram_fprd(datagram, slave->station_address, 0x0910,
                slave->base_dc_range == EC_DC_64 ? 8 : 4);
        ec_datagram_zero(datagram);
        fsm->retries = EC_FSM_RETRIES;
        fsm->state = ec_fsm_slave_scan_state_dc_cap;
    } else {
        ec_fsm_slave_scan_enter_datalink(fsm);
    }
}

/****************************************************************************/

/**
   Slave scan state: DC CAPABILITIES.
*/

void ec_fsm_slave_scan_state_dc_cap(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to receive system time datagram: ");
        ec_datagram_print_state(datagram);
        return;
    }

    if (datagram->working_counter == 1) {
        slave->has_dc_system_time = 1;
        EC_SLAVE_DBG(slave, 1, "Slave has the System Time register.\n");
    } else if (datagram->working_counter == 0) {
        EC_SLAVE_DBG(slave, 1, "Slave has no System Time register; delay "
                "measurement only.\n");
    } else {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to determine, if system time register is "
                "supported: ");
        ec_datagram_print_wc_error(datagram);
        return;
    }

    // read DC port receive times
    ec_datagram_fprd(datagram, slave->station_address, 0x0900, 16);
    ec_datagram_zero(datagram);
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_slave_scan_state_dc_times;
}

/****************************************************************************/

/**
   Slave scan state: DC TIMES.
*/

void ec_fsm_slave_scan_state_dc_times(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;
    int i;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to receive system time datagram: ");
        ec_datagram_print_state(datagram);
        return;
    }

    if (datagram->working_counter != 1) {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to get DC receive times: ");
        ec_datagram_print_wc_error(datagram);
        return;
    }

    for (i = 0; i < EC_MAX_PORTS; i++) {
        slave->ports[i].receive_time = EC_READ_U32(datagram->data + 4 * i);
    }

    ec_fsm_slave_scan_enter_datalink(fsm);
}

/****************************************************************************/

/**
   Slave scan entry function: DATALINK.
*/

void ec_fsm_slave_scan_enter_datalink(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    // read data link status
    ec_datagram_fprd(datagram, slave->station_address, 0x0110, 2);
    ec_datagram_zero(datagram);
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_slave_scan_state_datalink;
}

/****************************************************************************/

/** Enter slave scan state SII_SIZE.
 */
void ec_fsm_slave_scan_enter_sii_size(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    // Start fetching SII size

    EC_SLAVE_DBG(fsm->slave, 1, "Determining SII size.\n");

    fsm->sii_offset = EC_FIRST_SII_CATEGORY_OFFSET; // first category header
    ec_fsm_sii_read(&fsm->fsm_sii, fsm->slave, fsm->sii_offset,
            EC_FSM_SII_USE_CONFIGURED_ADDRESS);
    fsm->state = ec_fsm_slave_scan_state_sii_size;
    fsm->state(fsm); // execute state immediately
}

/****************************************************************************/

#ifdef EC_SII_ASSIGN

/** Enter slave scan state ASSIGN_SII.
 */
void ec_fsm_slave_scan_enter_assign_sii(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    EC_SLAVE_DBG(slave, 1, "Assigning SII access to EtherCAT.\n");

    // assign SII to ECAT
    ec_datagram_fpwr(datagram, slave->station_address, 0x0500, 1);
    EC_WRITE_U8(datagram->data, 0x00); // EtherCAT
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_slave_scan_state_assign_sii;
}

#endif

/****************************************************************************/

/** Enter slave scan state SII ident.
 */
void ec_fsm_slave_scan_enter_sii_ident(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    // Start fetching identification (alias/vendor/product/revision/serial)
    // to check for possible SII cache hits

    EC_SLAVE_DBG(fsm->slave, 1, "Loading SII identification words.\n");

    fsm->sii_offset = EC_SII_WORD_OFFSET_ALIAS;
    ec_fsm_sii_read(&fsm->fsm_sii, fsm->slave, fsm->sii_offset,
            EC_FSM_SII_USE_CONFIGURED_ADDRESS);
    fsm->state = ec_fsm_slave_scan_state_sii_ident;
    fsm->state(fsm); // execute state immediately
}

/****************************************************************************/

/**
   Slave scan state: DATALINK.
*/

void ec_fsm_slave_scan_state_datalink(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;
    uint16_t dl_status;
    unsigned int i;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to receive DL status datagram: ");
        ec_datagram_print_state(datagram);
        return;
    }

    if (datagram->working_counter != 1) {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to read DL status: ");
        ec_datagram_print_wc_error(datagram);
        return;
    }

    dl_status = EC_READ_U16(datagram->data);
    for (i = 0; i < EC_MAX_PORTS; i++) {
        slave->ports[i].link.link_up =
            dl_status & (1 << (4 + i)) ? 1 : 0;
        slave->ports[i].link.loop_closed =
            dl_status & (1 << (8 + i * 2)) ? 1 : 0;
        slave->ports[i].link.signal_detected =
            dl_status & (1 << (9 + i * 2)) ? 1 : 0;
    }

#ifdef EC_SII_ASSIGN
    ec_fsm_slave_scan_enter_assign_sii(fsm);
#else
    if (fsm->slave->master->sii_caching != EC_SII_DISABLE_CACHING) {
        ec_fsm_slave_scan_enter_sii_ident(fsm);
    }
    else {
        ec_fsm_slave_scan_enter_sii_size(fsm);
    }
#endif
}

/****************************************************************************/

#ifdef EC_SII_ASSIGN

/**
   Slave scan state: ASSIGN_SII.
*/

void ec_fsm_slave_scan_state_assign_sii(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--) {
        return;
    }

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        EC_SLAVE_WARN(slave, "Failed to receive SII assignment datagram: ");
        ec_datagram_print_state(datagram);
        // Try to go on, probably assignment is correct
        goto continue_with_sii_size;
    }

    if (datagram->working_counter != 1) {
        EC_SLAVE_WARN(slave, "Failed to assign SII to EtherCAT: ");
        ec_datagram_print_wc_error(datagram);
        // Try to go on, probably assignment is correct
    }

continue_with_sii_size:
    if (fsm->slave->master->sii_caching != EC_SII_DISABLE_CACHING) {
        ec_fsm_slave_scan_enter_sii_ident(fsm);
    }
    else {
        ec_fsm_slave_scan_enter_sii_size(fsm);
    }
}

#endif

/****************************************************************************/

/**
   Slave scan state: SII ident.
*/

void ec_fsm_slave_scan_state_sii_ident(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_slave_t *slave = fsm->slave;
    unsigned int words_fitting;
    int words_to_copy;
    uint32_t vendor, product, revision, serial;
    ec_sii_page_t *cached;

    if (ec_fsm_sii_exec(&fsm->fsm_sii)) {
        return;
    }

    if (!ec_fsm_sii_success(&fsm->fsm_sii)) {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to fetch SII identification.\n");
        return;
    }

    if (fsm->sii_offset == EC_SII_WORD_OFFSET_ALIAS) {
        // First step: Fetch alias from 0x0004, then go to 0x0008
        // unfortunately it makes no sense to read through, if only 2 words
        // are read in parallel (depends on the slave)
        slave->sii.alias = EC_READ_U16(fsm->fsm_sii.value);
        slave->effective_alias = slave->sii.alias;

        EC_SLAVE_DBG(slave, 1, "Alias %u\n", slave->sii.alias);

        // Continue with vendor ID
        fsm->sii_offset = EC_SII_WORD_OFFSET_VENDOR;
        ec_fsm_sii_read(&fsm->fsm_sii, slave, fsm->sii_offset,
                        EC_FSM_SII_USE_CONFIGURED_ADDRESS);
        ec_fsm_sii_exec(&fsm->fsm_sii); // execute state immediately
        return;
    }

    // Vendor ID and following
    words_fitting = EC_NUM_SII_IDENT_WORDS + EC_SII_WORD_OFFSET_VENDOR
        - fsm->sii_offset;
    words_to_copy = min(words_fitting, fsm->fsm_sii.read_word_count);
    memcpy(fsm->sii_ident + (fsm->sii_offset - EC_SII_WORD_OFFSET_VENDOR) * 2,
                    fsm->fsm_sii.value, words_to_copy * 2);

    if (fsm->sii_offset - EC_SII_WORD_OFFSET_VENDOR
            + fsm->fsm_sii.read_word_count < EC_NUM_SII_IDENT_WORDS) {
        // fetch the remaining words
        fsm->sii_offset += fsm->fsm_sii.read_word_count;
        ec_fsm_sii_read(&fsm->fsm_sii, slave, fsm->sii_offset,
                EC_FSM_SII_USE_CONFIGURED_ADDRESS);
        ec_fsm_sii_exec(&fsm->fsm_sii); // execute state immediately
        return;
    }

    // All identification words read

    vendor = EC_READ_U32(fsm->sii_ident);
    product = EC_READ_U32(fsm->sii_ident + 4);
    revision = EC_READ_U32(fsm->sii_ident + 8);
    serial = EC_READ_U32(fsm->sii_ident + 12);

    EC_SLAVE_DBG(slave, 1,
            "Identification 0x%08X / 0x%08X / 0x%08X / 0x%08X\n",
            vendor, product, revision, serial);

    cached = ec_master_find_cached_sii_page(slave->master, vendor,
            product, revision, serial, slave->sii.alias);
    if (cached) {
        int ret;

        EC_SLAVE_DBG(slave, 1, "Found matching SII page in cache.\n");

        ret = ec_sii_page_copy(&slave->sii_page, cached);
        if (ret) {
            EC_SLAVE_ERR(slave, "Failed to copy cached SII page.\n");
            fsm->slave->error_flag = 1;
            fsm->state = ec_fsm_slave_scan_state_error;
            return;
        }

        slave->sii_page.origin = EC_SII_PAGE_CACHED;

        // Evaluate the (cached) SII contents...
        if (ec_slave_analyze_sii_data(slave)) {
            EC_SLAVE_ERR(slave, "Failed to analyze category data.\n");
            fsm->slave->error_flag = 1;
            fsm->state = ec_fsm_slave_scan_state_error;
            return;
        }

#ifdef EC_REGALIAS
        ec_fsm_slave_scan_enter_regalias(fsm);
#else
        if (slave->sii.mailbox_protocols & EC_MBOX_COE) {
            ec_fsm_slave_scan_enter_preop(fsm);
        } else {
            fsm->state = ec_fsm_slave_scan_state_end;
        }
#endif
        return;
    }

    EC_SLAVE_DBG(slave, 1, "No matching SII page found in cache.\n");
    // We will have to read it out by ourselves. Sigh...
    ec_fsm_slave_scan_enter_sii_size(fsm);
}

/****************************************************************************/

/**
   Slave scan state: SII SIZE.
*/

void ec_fsm_slave_scan_state_sii_size(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_slave_t *slave = fsm->slave;
    uint16_t cat_type, cat_size;
    size_t word_count = 0;

    if (ec_fsm_sii_exec(&fsm->fsm_sii))
        return;

    if (!ec_fsm_sii_success(&fsm->fsm_sii)) {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to determine SII content size:"
                " Reading word offset 0x%04x failed. Assuming %u words.\n",
                fsm->sii_offset, EC_FIRST_SII_CATEGORY_OFFSET);
        word_count = EC_FIRST_SII_CATEGORY_OFFSET;
        goto alloc_sii;
    }

    cat_type = EC_READ_U16(fsm->fsm_sii.value);
    cat_size = EC_READ_U16(fsm->fsm_sii.value + 2);

    if (cat_type != 0xFFFF) { // not the last category
        off_t next_offset = 2UL + fsm->sii_offset + cat_size;

        EC_SLAVE_DBG(slave, 1, "Found category type %u with size %u."
                " Proceeding to offset %zd.\n",
                cat_type, cat_size, (ssize_t)next_offset);

        if (next_offset >= EC_MAX_SII_SIZE) {
            EC_SLAVE_WARN(slave, "SII size exceeds %u words"
                    " (0xffff limiter missing?).\n", EC_MAX_SII_SIZE);
            // cut off category data...
            word_count = EC_FIRST_SII_CATEGORY_OFFSET;
            goto alloc_sii;
        }
        fsm->sii_offset = next_offset;
        ec_fsm_sii_read(&fsm->fsm_sii, slave, fsm->sii_offset,
                        EC_FSM_SII_USE_CONFIGURED_ADDRESS);
        ec_fsm_sii_exec(&fsm->fsm_sii); // execute state immediately
        return;
    }

    word_count = fsm->sii_offset + 1;

alloc_sii:
    if (ec_sii_page_alloc(&slave->sii_page, word_count)) {
        EC_SLAVE_ERR(slave, "Failed to allocate %zu words of SII data.\n",
               word_count);
        slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        return;
    }

    // Start fetching SII contents

    fsm->state = ec_fsm_slave_scan_state_sii_data;
    fsm->sii_offset = 0x0000;
    ec_fsm_sii_read(&fsm->fsm_sii, slave, fsm->sii_offset,
            EC_FSM_SII_USE_CONFIGURED_ADDRESS);
    ec_fsm_sii_exec(&fsm->fsm_sii); // execute state immediately
}

/****************************************************************************/

/**
   Slave scan state: SII DATA.
*/

void ec_fsm_slave_scan_state_sii_data(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_slave_t *slave = fsm->slave;
    unsigned int words_fitting;
    int words_to_copy;

    if (ec_fsm_sii_exec(&fsm->fsm_sii)) return;

    if (!ec_fsm_sii_success(&fsm->fsm_sii)) {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to fetch SII contents.\n");
        return;
    }

    // Did we get 2 or 4 words?
    words_fitting = slave->sii_page.word_count - fsm->sii_offset;
    words_to_copy = min(words_fitting, fsm->fsm_sii.read_word_count);
    memcpy(slave->sii_page.words + fsm->sii_offset, fsm->fsm_sii.value,
            words_to_copy * 2);

    if (fsm->sii_offset + fsm->fsm_sii.read_word_count
            < slave->sii_page.word_count) {
        // fetch the next words
        fsm->sii_offset += fsm->fsm_sii.read_word_count;
        ec_fsm_sii_read(&fsm->fsm_sii, slave, fsm->sii_offset,
                        EC_FSM_SII_USE_CONFIGURED_ADDRESS);
        ec_fsm_sii_exec(&fsm->fsm_sii); // execute state immediately
        return;
    }

    slave->sii_page.origin = EC_SII_PAGE_FETCHED;

    // All words read. Evaluate the SII contents...
    if (ec_slave_analyze_sii_data(slave)) {
        EC_SLAVE_ERR(slave, "Failed to analyze category data.\n");
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
    }

    // Add the page that we just read to the cache
    ec_master_cache_sii_page(slave->master, &slave->sii_page);

#ifdef EC_REGALIAS
    ec_fsm_slave_scan_enter_regalias(fsm);
#else
    if (slave->sii.mailbox_protocols & EC_MBOX_COE) {
        ec_fsm_slave_scan_enter_preop(fsm);
    } else {
        fsm->state = ec_fsm_slave_scan_state_end;
    }
#endif
}

/****************************************************************************/

#ifdef EC_REGALIAS

/** Slave scan entry function: REGALIAS.
 */
void ec_fsm_slave_scan_enter_regalias(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    // read alias from register
    EC_SLAVE_DBG(slave, 1, "Reading alias from register.\n");
    ec_datagram_fprd(datagram, slave->station_address, 0x0012, 2);
    ec_datagram_zero(datagram);
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_slave_scan_state_regalias;
}

/****************************************************************************/

/** Slave scan state: REGALIAS.
 */
void ec_fsm_slave_scan_state_regalias(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to receive register alias datagram: ");
        ec_datagram_print_state(datagram);
        return;
    }

    if (datagram->working_counter != 1) {
        EC_SLAVE_DBG(slave, 1, "Failed to read register alias.\n");
    } else {
        slave->effective_alias = EC_READ_U16(datagram->data);
        EC_SLAVE_DBG(slave, 1, "Read alias %u from register.\n",
                slave->effective_alias);
    }

    if (slave->sii.mailbox_protocols & EC_MBOX_COE) {
        ec_fsm_slave_scan_enter_preop(fsm);
    } else {
        fsm->state = ec_fsm_slave_scan_state_end;
    }
}

#endif // defined EC_REGALIAS

/****************************************************************************/

/** Enter slave scan state PREOP.
 */
void ec_fsm_slave_scan_enter_preop(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_slave_t *slave = fsm->slave;
    uint8_t current_state = slave->current_state & EC_SLAVE_STATE_MASK;

    if (current_state != EC_SLAVE_STATE_PREOP
            && current_state != EC_SLAVE_STATE_SAFEOP
            && current_state != EC_SLAVE_STATE_OP) {
        if (slave->master->debug_level) {
            char str[EC_STATE_STRING_SIZE];
            ec_state_string(current_state, str, 0);
            EC_SLAVE_DBG(slave, 0, "Slave is not in the state"
                    " to do mailbox com (%s), setting to PREOP.\n", str);
        }

        fsm->state = ec_fsm_slave_scan_state_preop;
        ec_slave_request_state(slave, EC_SLAVE_STATE_PREOP);
        ec_fsm_slave_config_start(fsm->fsm_slave_config, slave);
        ec_fsm_slave_config_exec(fsm->fsm_slave_config);
    } else {
        EC_SLAVE_DBG(slave, 1, "Reading mailbox"
                " sync manager configuration.\n");

        /* Scan current sync manager configuration to get configured mailbox
         * sizes. */
        ec_datagram_fprd(fsm->datagram, slave->station_address, 0x0800,
                EC_SYNC_PAGE_SIZE * 2);
        fsm->retries = EC_FSM_RETRIES;
        fsm->state = ec_fsm_slave_scan_state_sync;
    }
}

/****************************************************************************/

/** Slave scan state: PREOP.
 */
void ec_fsm_slave_scan_state_preop(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    if (ec_fsm_slave_config_exec(fsm->fsm_slave_config))
        return;

    if (!ec_fsm_slave_config_success(fsm->fsm_slave_config)) {
        fsm->state = ec_fsm_slave_scan_state_error;
        return;
    }

    ec_fsm_slave_scan_enter_pdos(fsm);
}

/****************************************************************************/

/** Slave scan state: SYNC.
 */
void ec_fsm_slave_scan_state_sync(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;
    uint16_t tx_offset, tx_size, rx_offset, rx_size;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to receive sync manager"
                " configuration datagram: ");
        ec_datagram_print_state(datagram);
        return;
    }

    if (datagram->working_counter != 1) {
        fsm->slave->error_flag = 1;
        fsm->state = ec_fsm_slave_scan_state_error;
        EC_SLAVE_ERR(slave, "Failed to read DL status: ");
        ec_datagram_print_wc_error(datagram);
        return;
    }

    rx_offset = EC_READ_U16(datagram->data);
    rx_size = EC_READ_U16(datagram->data + 2);
    tx_offset = EC_READ_U16(datagram->data + 8);
    tx_size = EC_READ_U16(datagram->data + 10);

    if (rx_size == 0xffff) {
        fsm->state = ec_fsm_slave_scan_state_error;
        slave->sii.mailbox_protocols = 0x0000;
        EC_SLAVE_ERR(slave, "Invalid RX mailbox size (%u) configured."
                " Disabling mailbox communication.", rx_size);
        return;
    }

    if (tx_size == 0xffff) {
        fsm->state = ec_fsm_slave_scan_state_error;
        slave->sii.mailbox_protocols = 0x0000;
        EC_SLAVE_ERR(slave, "Invalid TX mailbox size (%u) configured."
                " Disabling mailbox communication.", tx_size);
        return;
    }

    slave->configured_rx_mailbox_offset = rx_offset;
    slave->configured_rx_mailbox_size = rx_size;
    slave->configured_tx_mailbox_offset = tx_offset;
    slave->configured_tx_mailbox_size = tx_size;

    EC_SLAVE_DBG(slave, 1, "Mailbox configuration:\n");
    EC_SLAVE_DBG(slave, 1, " RX offset=0x%04x size=%u\n",
            slave->configured_rx_mailbox_offset,
            slave->configured_rx_mailbox_size);
    EC_SLAVE_DBG(slave, 1, " TX offset=0x%04x size=%u\n",
            slave->configured_tx_mailbox_offset,
            slave->configured_tx_mailbox_size);

    ec_fsm_slave_scan_enter_pdos(fsm);
}

/****************************************************************************/

/** Enter slave scan state PDOS.
 */
void ec_fsm_slave_scan_enter_pdos(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    ec_slave_t *slave = fsm->slave;

    EC_SLAVE_DBG(slave, 1, "Scanning PDO assignment and mapping.\n");
    fsm->state = ec_fsm_slave_scan_state_pdos;
    ec_fsm_pdo_start_reading(fsm->fsm_pdo, slave);
    ec_fsm_pdo_exec(fsm->fsm_pdo, fsm->datagram); // execute immediately
}

/****************************************************************************/

/** Slave scan state: PDOS.
 */
void ec_fsm_slave_scan_state_pdos(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
    if (ec_fsm_pdo_exec(fsm->fsm_pdo, fsm->datagram)) {
        return;
    }

    if (!ec_fsm_pdo_success(fsm->fsm_pdo)) {
        fsm->state = ec_fsm_slave_scan_state_error;
        return;
    }

    // reading PDO configuration finished
    fsm->state = ec_fsm_slave_scan_state_end;
}

/*****************************************************************************
 * Common state functions
 ****************************************************************************/

/** State: ERROR.
 */
void ec_fsm_slave_scan_state_error(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
}

/****************************************************************************/

/** State: END.
 */
void ec_fsm_slave_scan_state_end(
        ec_fsm_slave_scan_t *fsm /**< slave state machine */
        )
{
}

/****************************************************************************/
