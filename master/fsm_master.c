/*****************************************************************************
 *
 *  Copyright (C) 2006-2023  Florian Pose, Ingenieurgemeinschaft IgH
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

/** \file
 * EtherCAT master state machine.
 */

/****************************************************************************/

#include "globals.h"
#include "master.h"
#include "mailbox.h"
#include "slave_config.h"
#ifdef EC_EOE
#include "ethernet.h"
#endif

#include "fsm_master.h"
#include "fsm_foe.h"

/****************************************************************************/

/** Time difference [ns] to tolerate without setting a new system time offset.
 */
#define EC_SYSTEM_TIME_TOLERANCE_NS 1000000

/****************************************************************************/

// prototypes for private methods
void ec_fsm_master_restart(ec_fsm_master_t *);
int ec_fsm_master_action_process_sii(ec_fsm_master_t *);
int ec_fsm_master_action_process_int_request(ec_fsm_master_t *);
void ec_fsm_master_action_idle(ec_fsm_master_t *);
void ec_fsm_master_action_next_slave_state(ec_fsm_master_t *);
void ec_fsm_master_action_configure(ec_fsm_master_t *);
u64 ec_fsm_master_dc_offset32(ec_fsm_master_t *, u64, u64, unsigned long);
u64 ec_fsm_master_dc_offset64(ec_fsm_master_t *, u64, u64, unsigned long);

/****************************************************************************/

void ec_fsm_master_state_start(ec_fsm_master_t *);
void ec_fsm_master_state_broadcast(ec_fsm_master_t *);
void ec_fsm_master_state_read_state(ec_fsm_master_t *);
/* state_acknowledge and state_configure_slave have moved into the
 * per-slave fsm_slave in sync with fsm_slave_scan / fsm_slave_config. */
void ec_fsm_master_state_clear_addresses(ec_fsm_master_t *);
void ec_fsm_master_state_dc_measure_delays(ec_fsm_master_t *);
void ec_fsm_master_state_scan_slave(ec_fsm_master_t *);
void ec_fsm_master_state_dc_read_offset(ec_fsm_master_t *);
void ec_fsm_master_state_dc_write_offset(ec_fsm_master_t *);
void ec_fsm_master_state_assign_sii(ec_fsm_master_t *);
void ec_fsm_master_state_write_sii(ec_fsm_master_t *);
void ec_fsm_master_state_sdo_dictionary(ec_fsm_master_t *);
void ec_fsm_master_state_sdo_request(ec_fsm_master_t *);
void ec_fsm_master_state_soe_request(ec_fsm_master_t *);

void ec_fsm_master_enter_dc_read_old_times(ec_fsm_master_t *);
void ec_fsm_master_state_dc_read_old_times(ec_fsm_master_t *);
void ec_fsm_master_enter_clear_addresses(ec_fsm_master_t *);
void ec_fsm_master_enter_write_system_times(ec_fsm_master_t *);

/****************************************************************************/

/** Constructor.
 */
void ec_fsm_master_init(
        ec_fsm_master_t *fsm, /**< Master state machine. */
        ec_master_t *master, /**< EtherCAT master. */
        ec_datagram_t *datagram /**< Datagram object to use. */
        )
{
    fsm->master = master;
    fsm->datagram = datagram;

    // inits the member variables state, idle, dev_idx, link_state,
    // slaves_responding, slave_states and rescan_required
    ec_fsm_master_reset(fsm);

    fsm->retries = 0;
    fsm->scan_jiffies = 0;
    fsm->slave = NULL;
    fsm->sii_request = NULL;
    fsm->sii_index = 0;
    fsm->sdo_request = NULL;
    fsm->soe_request = NULL;

    // init sub-state-machines
    ec_fsm_coe_init(&fsm->fsm_coe);
    ec_fsm_soe_init(&fsm->fsm_soe);
    ec_fsm_pdo_init(&fsm->fsm_pdo, &fsm->fsm_coe);
#ifdef EC_EOE
    ec_fsm_eoe_init(&fsm->fsm_eoe);
#endif
    ec_fsm_change_init(&fsm->fsm_change);
    /* Per-slave fsm_slave now carries its own fsm_slave_scan /
     * fsm_slave_config, so the master FSM no longer needs them. */
    ec_fsm_sii_init(&fsm->fsm_sii);
}

/****************************************************************************/

/** Destructor.
 */
void ec_fsm_master_clear(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    // clear sub-state machines
    ec_fsm_coe_clear(&fsm->fsm_coe);
    ec_fsm_soe_clear(&fsm->fsm_soe);
    ec_fsm_pdo_clear(&fsm->fsm_pdo);
#ifdef EC_EOE
    ec_fsm_eoe_clear(&fsm->fsm_eoe);
#endif
    ec_fsm_change_clear(&fsm->fsm_change);
    ec_fsm_sii_clear(&fsm->fsm_sii);
}

/****************************************************************************/

/** Reset state machine.
 */
void ec_fsm_master_reset(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_device_index_t dev_idx;

    fsm->state = ec_fsm_master_state_start;
    fsm->idle = 0;
    fsm->dev_idx = EC_DEVICE_MAIN;

    for (dev_idx = EC_DEVICE_MAIN;
            dev_idx < ec_master_num_devices(fsm->master); dev_idx++) {
        fsm->link_state[dev_idx] = 0;
        fsm->slaves_responding[dev_idx] = 0;
        fsm->slave_states[dev_idx] = EC_SLAVE_STATE_UNKNOWN;
    }

    fsm->rescan_required = 0;
}

/****************************************************************************/

/** Executes the current state of the state machine.
 *
 * If the state machine's datagram is not sent or received yet, the execution
 * of the state machine is delayed to the next cycle.
 *
 * \return true, if the state machine was executed
 */
int ec_fsm_master_exec(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    if (fsm->datagram->state == EC_DATAGRAM_SENT
        || fsm->datagram->state == EC_DATAGRAM_QUEUED) {
        // datagram was not sent or received yet.
        return 0;
    }

    fsm->state(fsm);
    return 1;
}

/****************************************************************************/

/**
 * \return true, if the state machine is in an idle phase
 */
int ec_fsm_master_idle(
        const ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    return fsm->idle;
}

/****************************************************************************/

/** Restarts the master state machine.
 */
void ec_fsm_master_restart(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    fsm->dev_idx = EC_DEVICE_MAIN;
    fsm->state = ec_fsm_master_state_start;
    fsm->state(fsm); // execute immediately
}

/*****************************************************************************
 * Master state machine
 ****************************************************************************/

/** Master state: START.
 *
 * Starts with getting slave count and slave states.
 */
void ec_fsm_master_state_start(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;

    fsm->idle = 1;

    // check for emergency requests
    if (!list_empty(&master->emerg_reg_requests)) {
        ec_reg_request_t *request;

        // get first request
        request = list_entry(master->emerg_reg_requests.next,
                ec_reg_request_t, list);
        list_del_init(&request->list); // dequeue
        request->state = EC_INT_REQUEST_BUSY;

        if (request->transfer_size > fsm->datagram->mem_size) {
            EC_MASTER_ERR(master, "Emergency request data too large!\n");
            request->state = EC_INT_REQUEST_FAILURE;
            wake_up_all(&master->request_queue);
            fsm->state(fsm); // continue
            return;
        }

        if (request->dir != EC_DIR_OUTPUT) {
            EC_MASTER_ERR(master, "Emergency requests must be"
                    " write requests!\n");
            request->state = EC_INT_REQUEST_FAILURE;
            wake_up_all(&master->request_queue);
            fsm->state(fsm); // continue
            return;
        }

        EC_MASTER_DBG(master, 1, "Writing emergency register request...\n");
        ec_datagram_apwr(fsm->datagram, request->ring_position,
                request->address, request->transfer_size);
        memcpy(fsm->datagram->data, request->data, request->transfer_size);
        fsm->datagram->device_index = EC_DEVICE_MAIN;
        request->state = EC_INT_REQUEST_SUCCESS;
        wake_up_all(&master->request_queue);
        return;
    }

    ec_datagram_brd(fsm->datagram, 0x0130, 2);
    ec_datagram_zero(fsm->datagram);
    fsm->datagram->device_index = fsm->dev_idx;
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_master_state_broadcast;
}

/****************************************************************************/

/** Master state: BROADCAST.
 *
 * Processes the broadcast read slave count and slaves states.
 */
void ec_fsm_master_state_broadcast(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    unsigned int i, size;
    ec_slave_t *slave;
    ec_master_t *master = fsm->master;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--) {
        return;
    }

    // bus topology change?
    if (datagram->working_counter != fsm->slaves_responding[fsm->dev_idx]) {
        fsm->rescan_required = 1;
        fsm->slaves_responding[fsm->dev_idx] = datagram->working_counter;
        EC_MASTER_INFO(master, "%u slave(s) responding on %s device. "
                "Re-scanning on next possibility.\n",
                fsm->slaves_responding[fsm->dev_idx],
                ec_device_names[fsm->dev_idx != 0]);
    }

    if (fsm->link_state[fsm->dev_idx] &&
            !master->devices[fsm->dev_idx].link_state) {
        ec_device_index_t dev_idx;

        EC_MASTER_DBG(master, 1, "Master state machine detected "
                "link down on %s device. Clearing slave list.\n",
                ec_device_names[fsm->dev_idx != 0]);

#ifdef EC_EOE
        ec_master_eoe_stop(master);
        ec_master_clear_eoe_handlers(master);
#endif
        ec_master_clear_slaves(master);

        for (dev_idx = EC_DEVICE_MAIN;
                dev_idx < ec_master_num_devices(master); dev_idx++) {
            fsm->slave_states[dev_idx] = 0x00;
            fsm->slaves_responding[dev_idx] = 0; /* Reset to trigger rescan on
                                                    next link up. */
        }
    }
    fsm->link_state[fsm->dev_idx] = master->devices[fsm->dev_idx].link_state;

    if (datagram->state == EC_DATAGRAM_RECEIVED &&
            fsm->slaves_responding[fsm->dev_idx]) {
        uint8_t states = EC_READ_U8(datagram->data);
        if (states != fsm->slave_states[fsm->dev_idx]) {
            // slave states changed
            char state_str[EC_STATE_STRING_SIZE];
            fsm->slave_states[fsm->dev_idx] = states;
            ec_state_string(states, state_str, 1);
            EC_MASTER_INFO(master, "Slave states on %s device: %s.\n",
                    ec_device_names[fsm->dev_idx != 0], state_str);
        }
    } else {
        fsm->slave_states[fsm->dev_idx] = 0x00;
    }

    fsm->dev_idx++;
    if (fsm->dev_idx < ec_master_num_devices(master)) {
        // check number of responding slaves on next device
        fsm->state = ec_fsm_master_state_start;
        fsm->state(fsm); // execute immediately
        return;
    }

    if (fsm->rescan_required) {
        down(&master->scan_sem);
        if (!master->allow_scan) {
            up(&master->scan_sem);
        } else {
            unsigned int count = 0, next_dev_slave, ring_position;
            ec_device_index_t dev_idx;

            master->scan_busy = 1;
            master->scan_index = 0;
            up(&master->scan_sem);

            EC_MASTER_INFO(master, "Re-scanning now.\n");

            // clear all slaves and scan the bus
            fsm->rescan_required = 0;
            fsm->idle = 0;
            fsm->scan_jiffies = jiffies;

#ifdef EC_EOE
            ec_master_eoe_stop(master);
            ec_master_clear_eoe_handlers(master);
#endif
            ec_master_clear_slaves(master);

            for (dev_idx = EC_DEVICE_MAIN;
                    dev_idx < ec_master_num_devices(master); dev_idx++) {
                count += fsm->slaves_responding[dev_idx];
            }

            if (!count) {
                // no slaves present -> finish state machine.
                master->scan_busy = 0;
                wake_up_interruptible(&master->scan_queue);
                ec_fsm_master_restart(fsm);
                return;
            }

            size = sizeof(ec_slave_t) * count;
            if (!(master->slaves =
                        (ec_slave_t *) kmalloc(size, GFP_KERNEL))) {
                EC_MASTER_ERR(master, "Failed to allocate %u bytes"
                        " of slave memory!\n", size);
                master->scan_busy = 0;
                wake_up_interruptible(&master->scan_queue);
                ec_fsm_master_restart(fsm);
                return;
            }

            // init slaves
            dev_idx = EC_DEVICE_MAIN;
            next_dev_slave = fsm->slaves_responding[dev_idx];
            ring_position = 0;
            for (i = 0; i < count; i++, ring_position++) {
                slave = master->slaves + i;
                while (i >= next_dev_slave) {
                    dev_idx++;
                    next_dev_slave += fsm->slaves_responding[dev_idx];
                    ring_position = 0;
                }

                ec_slave_init(slave, master, dev_idx, ring_position, i + 1);

                // do not force reconfiguration in operation phase to avoid
                // unnecesssary process data interruptions
                if (master->phase != EC_OPERATION) {
                    slave->force_config = 1;
                }
            }
            master->slave_count = count;
            master->fsm_slave = master->slaves;

            ec_fsm_master_enter_dc_read_old_times(fsm);
            return;
        }
    }

    if (master->slave_count) {
        ec_slave_t *s;
        int scan_pending = 0;

        /* Sanity check: if any slave still has scan_required=1 we must
         * finish scanning before we touch DC / PDO / FMMU - the old
         * code path happily jumped to write_system_times on
         * config_changed, which left unscanned slaves with
         * base_fmmu_count=0 and every subsequent configuration aborted
         * in enter_fmmu with "Slave has less FMMUs (0)". */
        for (s = master->slaves;
                s < master->slaves + master->slave_count;
                s++) {
            if (s->scan_required && !s->error_flag) {
                scan_pending = 1;
                break;
            }
        }

        if (scan_pending) {
            EC_MASTER_DBG(master, 1,
                    "Configuration change deferred until scan completes.\n");
            /* Kick every slave into state_ready so its action_scan can
             * finish the outstanding scan; then wait in state_scan_slave
             * until all scan_required flags are clear before rejoining
             * the configure path. */
            down(&master->scan_sem);
            master->scan_busy = 1;
            up(&master->scan_sem);
            fsm->scan_jiffies = jiffies;
            for (s = master->slaves;
                    s < master->slaves + master->slave_count;
                    s++) {
                ec_fsm_slave_set_ready(&s->fsm);
            }
            master->scan_index = 0;
            fsm->state = ec_fsm_master_state_scan_slave;
            fsm->datagram->state = EC_DATAGRAM_INVALID;
            fsm->state(fsm); // execute immediately
            return;
        }

        // application applied configurations
        if (master->config_changed) {
            master->config_changed = 0;

            EC_MASTER_DBG(master, 1, "Configuration changed.\n");

            fsm->slave = master->slaves; // begin with first slave
            ec_fsm_master_enter_write_system_times(fsm);

        } else {
            // fetch state from first slave
            fsm->slave = master->slaves;
            ec_datagram_fprd(fsm->datagram, fsm->slave->station_address,
                    0x0130, 2);
            ec_datagram_zero(datagram);
            fsm->datagram->device_index = fsm->slave->device_index;
            fsm->retries = EC_FSM_RETRIES;
            fsm->state = ec_fsm_master_state_read_state;
        }
    } else {
        ec_fsm_master_restart(fsm);
    }
}

/****************************************************************************/

/** Check for pending SII write requests and process one.
 *
 * \return non-zero, if an SII write request is processed.
 */
int ec_fsm_master_action_process_sii(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;
    ec_sii_write_request_t *request;
    ec_slave_config_t *config;
    ec_flag_t *flag;
    int assign_to_pdi;

    // search the first request to be processed
    while (1) {
        if (list_empty(&master->sii_requests))
            break;

        // get first request
        request = list_entry(master->sii_requests.next,
                ec_sii_write_request_t, list);
        list_del_init(&request->list); // dequeue
        request->state = EC_INT_REQUEST_BUSY;

        assign_to_pdi = 0;
        config = request->slave->config;
        if (config) {
            flag = ec_slave_config_find_flag(config, "AssignToPdi");
            if (flag) {
                assign_to_pdi = flag->value;
            }
        }

        if (assign_to_pdi) {
            fsm->sii_request = request;
            EC_SLAVE_DBG(request->slave, 1,
                    "Assigning SII back to EtherCAT.\n");
            ec_datagram_fpwr(fsm->datagram, request->slave->station_address,
                    0x0500, 0x01);
            EC_WRITE_U8(fsm->datagram->data, 0x00); // EtherCAT
            fsm->retries = EC_FSM_RETRIES;
            fsm->state = ec_fsm_master_state_assign_sii;
            return 1;
        }

        // found pending SII write operation. execute it!
        EC_SLAVE_DBG(request->slave, 1, "Writing SII data...\n");
        fsm->sii_request = request;
        fsm->sii_index = 0;
        ec_fsm_sii_write(&fsm->fsm_sii, request->slave, request->offset,
                request->words, EC_FSM_SII_USE_CONFIGURED_ADDRESS);
        fsm->state = ec_fsm_master_state_write_sii;
        fsm->state(fsm); // execute immediately
        return 1;
    }

    return 0;
}

/****************************************************************************/

/** Check for pending internal SDO/SoE requests and process one.
 *
 * \return non-zero, if an SDO request is processed.
 */
int ec_fsm_master_action_process_int_request(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;
    ec_slave_t *slave;
    ec_sdo_request_t *sdo_req;
    ec_soe_request_t *soe_req;

    // search for internal requests to be processed
    for (slave = master->slaves;
            slave < master->slaves + master->slave_count;
            slave++) {

        if (!slave->config) {
            continue;
        }

        /* Same race guard as the dictionary fetch: skip a slave whose
         * per-slave configuration FSM is still driving its CoE mailbox. */
        if (ec_fsm_slave_config_running(&slave->fsm.fsm_slave_config)) {
            continue;
        }

        list_for_each_entry(sdo_req, &slave->config->sdo_requests, list) {
            if (sdo_req->state == EC_INT_REQUEST_QUEUED) {

                if (ec_sdo_request_timed_out(sdo_req)) {
                    sdo_req->state = EC_INT_REQUEST_FAILURE;
                    EC_SLAVE_DBG(slave, 1, "Internal SDO request"
                            " timed out.\n");
                    continue;
                }

                if (slave->current_state == EC_SLAVE_STATE_INIT) {
                    sdo_req->state = EC_INT_REQUEST_FAILURE;
                    continue;
                }

                sdo_req->state = EC_INT_REQUEST_BUSY;
                EC_SLAVE_DBG(slave, 1, "Processing internal"
                        " SDO request...\n");
                fsm->idle = 0;
                fsm->sdo_request = sdo_req;
                fsm->slave = slave;
                fsm->state = ec_fsm_master_state_sdo_request;
                ec_fsm_coe_transfer(&fsm->fsm_coe, slave, sdo_req);
                ec_fsm_coe_exec(&fsm->fsm_coe, fsm->datagram);
                return 1;
            }
        }

        list_for_each_entry(soe_req, &slave->config->soe_requests, list) {
            if (soe_req->state == EC_INT_REQUEST_QUEUED) {

                if (ec_soe_request_timed_out(soe_req)) {
                    soe_req->state = EC_INT_REQUEST_FAILURE;
                    EC_SLAVE_DBG(slave, 1, "Internal SoE request"
                            " timed out.\n");
                    continue;
                }

                if (slave->current_state == EC_SLAVE_STATE_INIT) {
                    soe_req->state = EC_INT_REQUEST_FAILURE;
                    continue;
                }

                soe_req->state = EC_INT_REQUEST_BUSY;
                EC_SLAVE_DBG(slave, 1, "Processing internal"
                        " SoE request...\n");
                fsm->idle = 0;
                fsm->soe_request = soe_req;
                fsm->slave = slave;
                fsm->state = ec_fsm_master_state_soe_request;
                ec_fsm_soe_transfer(&fsm->fsm_soe, slave, soe_req);
                ec_fsm_soe_exec(&fsm->fsm_soe, fsm->datagram);
                return 1;
            }
        }
    }
    return 0;
}

/****************************************************************************/

/** Master action: IDLE.
 *
 * Does secondary work.
 */
void ec_fsm_master_action_idle(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;
    ec_slave_t *slave;

    /* With per-slave parallel configuration the master FSM no longer
     * owns a single "configuring" phase - config_busy stays raised while
     * any slave's fsm is in state_config. Clear it here once every
     * slave has dropped back out of state_config so blocked callers
     * (e.g. ecrt_master_activate waiting on config_queue) get woken up. */
    if (master->config_busy) {
        int still_configuring = 0;
        ec_slave_t *s;
        for (s = master->slaves;
                s < master->slaves + master->slave_count; s++) {
            if (ec_fsm_slave_config_running(&s->fsm.fsm_slave_config)) {
                still_configuring = 1;
                break;
            }
        }
        if (!still_configuring) {
            down(&master->config_sem);
            master->config_busy = 0;
            up(&master->config_sem);
            wake_up_interruptible(&master->config_queue);
        }
    }

    // Check for pending internal SDO or SoE requests
    if (ec_fsm_master_action_process_int_request(fsm)) {
        return;
    }

    // check, if slaves have an SDO dictionary to read out.
    for (slave = master->slaves;
            slave < master->slaves + master->slave_count;
            slave++) {
        /* With parallel slave configuration the per-slave FSM may
         * still be talking to the slave's CoE mailbox. Starting the
         * master-side dictionary fetch at the same time would issue
         * a second CoE request to the same mailbox, racing with the
         * configuration traffic. Skip this slave until its config
         * has finished. */
        if (ec_fsm_slave_config_running(&slave->fsm.fsm_slave_config)) {
            continue;
        }
        if (!(slave->sii.mailbox_protocols & EC_MBOX_COE)
                || (slave->sii.has_general
                    && !slave->sii.coe_details.enable_sdo_info)
                || slave->sdo_dictionary_fetched
                || slave->current_state == EC_SLAVE_STATE_INIT
                || slave->current_state == EC_SLAVE_STATE_UNKNOWN
                || jiffies - slave->jiffies_preop < EC_WAIT_SDO_DICT * HZ
                ) {
            /* Dictionary fetch is not going to happen (not supported,
             * already done, or the slave is still coming up). Enable
             * external request processing now so applications do not wait
             * forever on a slave whose dictionary we will never read. */
            if (!(slave->sii.mailbox_protocols & EC_MBOX_COE)
                    || (slave->sii.has_general
                        && !slave->sii.coe_details.enable_sdo_info)
                    || slave->sdo_dictionary_fetched) {
                ec_fsm_slave_set_ready(&slave->fsm);
            }
            continue;
        }

        EC_SLAVE_DBG(slave, 1, "Fetching SDO dictionary.\n");

        slave->sdo_dictionary_fetched = 1;

        // start fetching SDO dictionary
        fsm->idle = 0;
        fsm->slave = slave;
        fsm->state = ec_fsm_master_state_sdo_dictionary;
        ec_fsm_coe_dictionary(&fsm->fsm_coe, slave);
        ec_fsm_coe_exec(&fsm->fsm_coe, fsm->datagram); // execute immediately
        fsm->datagram->device_index = fsm->slave->device_index;
        return;
    }

    // check for pending SII write operations.
    if (ec_fsm_master_action_process_sii(fsm)) {
        return; // SII write request found
    }

    ec_fsm_master_restart(fsm);
}

/****************************************************************************/

/** Master action: Get state of next slave.
 */
void ec_fsm_master_action_next_slave_state(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;

    // is there another slave to query?
    fsm->slave++;
    if (fsm->slave < master->slaves + master->slave_count) {
        // fetch state from next slave
        fsm->idle = 1;
        ec_datagram_fprd(fsm->datagram,
                fsm->slave->station_address, 0x0130, 2);
        ec_datagram_zero(fsm->datagram);
        fsm->datagram->device_index = fsm->slave->device_index;
        fsm->retries = EC_FSM_RETRIES;
        fsm->state = ec_fsm_master_state_read_state;
        return;
    }

    // all slaves processed
    ec_fsm_master_action_idle(fsm);
}

/****************************************************************************/

/** Master action: Configure.
 */
void ec_fsm_master_action_configure(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;
    ec_slave_t *slave = fsm->slave;

    if (master->config_changed) {
        master->config_changed = 0;

        // abort iterating through slaves,
        // first compensate DC system time offsets,
        // then begin configuring at slave 0
        EC_MASTER_DBG(master, 1, "Configuration changed"
                " (aborting state check).\n");

        fsm->slave = master->slaves; // begin with first slave
        ec_fsm_master_enter_write_system_times(fsm);
        return;
    }

    /* The per-slave fsm_slave now owns scan, ACK and configuration.
     * All we need to do here is make sure the slave FSM is out of
     * state_idle so that state_ready's action_config gets a chance
     * to run on the next tick. */
    ec_fsm_slave_set_ready(&slave->fsm);

    // process next slave
    ec_fsm_master_action_next_slave_state(fsm);
}

/****************************************************************************/

/** Master state: READ STATE.
 *
 * Fetches the AL state of a slave.
 */
void ec_fsm_master_state_read_state(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_datagram_t *datagram = fsm->datagram;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--) {
        return;
    }

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        EC_SLAVE_ERR(slave, "Failed to receive AL state datagram: ");
        ec_datagram_print_state(datagram);
        ec_fsm_master_restart(fsm);
        return;
    }

    // did the slave not respond to its station address?
    if (datagram->working_counter != 1) {
        if (!slave->error_flag) {
            slave->error_flag = 1;
            EC_SLAVE_DBG(slave, 1, "Slave did not respond to state query.\n");
        }
        ec_fsm_master_restart(fsm);
        return;
    }

    // A single slave responded
    ec_slave_set_state(slave, EC_READ_U8(datagram->data));

    if (!slave->error_flag) {
        /* ACK_ERR handling is done by the per-slave fsm_slave's
         * state_ready path (via fsm_change MODE_ACK_ONLY) so we do not
         * race the same AL control register (0x0120) from two
         * different fsm_change instances here. The master FSM just
         * reads state and kicks configuration. */
        ec_fsm_master_action_configure(fsm);
        return;
    }

    // slave has error flag set; process next one
    ec_fsm_master_action_next_slave_state(fsm);
}

/****************************************************************************/

/** Start reading the DC port receive counters before clearing addresses.
 *
 * Gives ec_fsm_slave_scan_state_dc_times() a baseline to compare against
 * so it can flag ports whose timestamp did not change during the
 * subsequent broadcast as bypassed.
 */
void ec_fsm_master_enter_dc_read_old_times(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;

    if (!master->slave_count) {
        // nothing to read
        fsm->dev_idx = EC_DEVICE_MAIN;
        while (!fsm->slaves_responding[fsm->dev_idx]) {
            fsm->dev_idx++;
        }
        ec_fsm_master_enter_clear_addresses(fsm);
        return;
    }

    EC_MASTER_DBG(master, 1, "Reading old port receive times...\n");

    fsm->slave = master->slaves;
    // station addresses not assigned yet, use APRD
    ec_datagram_aprd(fsm->datagram, fsm->slave->ring_position, 0x0900, 16);
    ec_datagram_zero(fsm->datagram);
    fsm->datagram->device_index = fsm->slave->device_index;
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_master_state_dc_read_old_times;
}

/****************************************************************************/

/** Master state: DC_READ_OLD_TIMES.
 */
void ec_fsm_master_state_dc_read_old_times(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;
    ec_slave_t *slave = fsm->slave;
    ec_datagram_t *datagram = fsm->datagram;
    int i;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--) {
        return;
    }

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        EC_SLAVE_ERR(slave,
                "Failed to receive DC receive-times datagram: ");
        ec_datagram_print_state(datagram);
        // fall through, keep zeros
    } else if (datagram->working_counter != 1) {
        // slave may not support these registers; not fatal
        EC_SLAVE_DBG(slave, 1,
                "DC receive-times read returned wc %u.\n",
                datagram->working_counter);
    } else {
        for (i = 0; i < EC_MAX_PORTS; i++) {
            slave->ports[i].receive_time =
                EC_READ_U32(datagram->data + 4 * i);
        }
    }

    ++fsm->slave;
    if (fsm->slave < master->slaves + master->slave_count) {
        ec_datagram_aprd(datagram, fsm->slave->ring_position, 0x0900, 16);
        ec_datagram_zero(datagram);
        datagram->device_index = fsm->slave->device_index;
        fsm->retries = EC_FSM_RETRIES;
        return;
    }

    // all slaves read, proceed to clear addresses
    fsm->dev_idx = EC_DEVICE_MAIN;
    while (!fsm->slaves_responding[fsm->dev_idx]) {
        fsm->dev_idx++;
    }
    fsm->slave = master->slaves;
    ec_fsm_master_enter_clear_addresses(fsm);
}

/****************************************************************************/

/** Start clearing slave addresses.
 */
void ec_fsm_master_enter_clear_addresses(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    // broadcast clear all station addresses
    ec_datagram_bwr(fsm->datagram, 0x0010, 2);
    EC_WRITE_U16(fsm->datagram->data, 0x0000);
    fsm->datagram->device_index = fsm->dev_idx;
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_master_state_clear_addresses;
}

/****************************************************************************/

/** Master state: CLEAR ADDRESSES.
 */
void ec_fsm_master_state_clear_addresses(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;
    ec_datagram_t *datagram = fsm->datagram;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--) {
        return;
    }

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        EC_MASTER_ERR(master, "Failed to receive address"
                " clearing datagram on %s link: ",
                ec_device_names[fsm->dev_idx != 0]);
        ec_datagram_print_state(datagram);
        master->scan_busy = 0;
        master->scan_index = master->slave_count;
        wake_up_interruptible(&master->scan_queue);
        ec_fsm_master_restart(fsm);
        return;
    }

    if (datagram->working_counter != fsm->slaves_responding[fsm->dev_idx]) {
        EC_MASTER_WARN(master, "Failed to clear station addresses on %s link:"
                " Cleared %u of %u",
                ec_device_names[fsm->dev_idx != 0], datagram->working_counter,
                fsm->slaves_responding[fsm->dev_idx]);
    }

    EC_MASTER_DBG(master, 1, "Sending broadcast-write"
            " to measure transmission delays on %s link.\n",
            ec_device_names[fsm->dev_idx != 0]);

    ec_datagram_bwr(datagram, 0x0900, 1);
    ec_datagram_zero(datagram);
    fsm->datagram->device_index = fsm->dev_idx;
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_master_state_dc_measure_delays;
}

/****************************************************************************/

/** Master state: DC MEASURE DELAYS.
 */
void ec_fsm_master_state_dc_measure_delays(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;
    ec_datagram_t *datagram = fsm->datagram;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--) {
        return;
    }

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        EC_MASTER_ERR(master, "Failed to receive delay measuring datagram"
                " on %s link: ", ec_device_names[fsm->dev_idx != 0]);
        ec_datagram_print_state(datagram);
        master->scan_busy = 0;
        master->scan_index = master->slave_count;
        wake_up_interruptible(&master->scan_queue);
        ec_fsm_master_restart(fsm);
        return;
    }

    EC_MASTER_DBG(master, 1, "%u slaves responded to delay measuring"
            " on %s link.\n",
            datagram->working_counter, ec_device_names[fsm->dev_idx != 0]);

    do {
        fsm->dev_idx++;
    } while (fsm->dev_idx < ec_master_num_devices(master) &&
            !fsm->slaves_responding[fsm->dev_idx]);
    if (fsm->dev_idx < ec_master_num_devices(master)) {
        ec_fsm_master_enter_clear_addresses(fsm);
        return;
    }

    EC_MASTER_INFO(master, "Scanning bus.\n");

    // Hand scanning over to the per-slave FSMs. Each slave's
    // fsm_slave picks up its scan_required flag on its next tick
    // and drives fsm_slave_scan on the external datagram ring, so
    // all scans now run in parallel.
    {
        ec_slave_t *slave;
        for (slave = master->slaves;
                slave < master->slaves + master->slave_count;
                slave++) {
            ec_fsm_slave_set_ready(&slave->fsm);
        }
    }
    master->scan_index = 0;
    fsm->state = ec_fsm_master_state_scan_slave;
    fsm->datagram->state = EC_DATAGRAM_INVALID; // nothing to send
    fsm->state(fsm); // execute immediately
}

/****************************************************************************/

/** Master state: SCAN SLAVE.
 *
 * Waits until every per-slave fsm_slave has cleared its scan_required
 * flag (or the slave got flagged as broken).
 */
void ec_fsm_master_state_scan_slave(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;
    ec_slave_t *slave;

    for (slave = master->slaves;
            slave < master->slaves + master->slave_count;
            slave++) {
        if (slave->scan_required && !slave->error_flag) {
            // still in progress
            return;
        }
    }

    EC_MASTER_INFO(master, "Bus scanning completed in %lu ms.\n",
            (jiffies - fsm->scan_jiffies) * 1000 / HZ);

    master->scan_busy = 0;
    master->scan_index = master->slave_count;
    wake_up_interruptible(&master->scan_queue);

    // Attach slave configurations
    ec_master_attach_slave_configs(master);

    // Calculate DC (needs attached slaves due to reference clock selection)
    ec_master_calc_dc(master);

#ifdef EC_EOE
    // check if EoE processing has to be started
    ec_master_eoe_start(master);
#endif

    if (master->slave_count) {
        master->config_changed = 0;

        fsm->slave = master->slaves; // begin with first slave
        ec_fsm_master_enter_write_system_times(fsm);
    } else {
        ec_fsm_master_restart(fsm);
    }
}

/****************************************************************************/

/** Start writing DC system times.
 */
void ec_fsm_master_enter_write_system_times(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;

    if (master->dc_ref_time) {

        while (fsm->slave < master->slaves + master->slave_count) {
            if (!fsm->slave->base_dc_supported
                    || !fsm->slave->has_dc_system_time) {
                fsm->slave++;
                continue;
            }

            EC_SLAVE_DBG(fsm->slave, 1, "Checking system time offset.\n");

            // read DC system time (0x0910, 64 bit)
            //                         gap (64 bit)
            //     and time offset (0x0920, 64 bit)
            ec_datagram_fprd(fsm->datagram, fsm->slave->station_address,
                    0x0910, 24);
            fsm->datagram->device_index = fsm->slave->device_index;
            fsm->retries = EC_FSM_RETRIES;
            fsm->state = ec_fsm_master_state_dc_read_offset;
            return;
        }

    } else {
        if (master->active) {
            EC_MASTER_WARN(master, "No application time received up to now,"
                    " but master already active.\n");
        } else {
            EC_MASTER_DBG(master, 1, "No app_time received up to now.\n");
        }
    }

    // scanning and setting system times complete
    ec_master_request_op(master);
    EC_MASTER_DBG(master, 1, "After requesting OP, rescan_required is %u.\n",
            fsm->rescan_required);
    ec_fsm_master_restart(fsm);
}

/****************************************************************************/

/** Configure 32 bit time offset.
 *
 * \return New offset.
 */
u64 ec_fsm_master_dc_offset32(
        ec_fsm_master_t *fsm, /**< Master state machine. */
        u64 system_time, /**< System time register. */
        u64 old_offset, /**< Time offset register. */
        unsigned long jiffies_since_read /**< Jiffies for correction. */
        )
{
    ec_slave_t *slave = fsm->slave;
    u32 correction, system_time32, old_offset32, new_offset;
    s32 time_diff;

    system_time32 = (u32) system_time;
    old_offset32 = (u32) old_offset;

    // correct read system time by elapsed time since read operation
    correction = jiffies_since_read * 1000 / HZ * 1000000;
    system_time32 += correction;
    time_diff = (u32) slave->master->app_time - system_time32;

    EC_SLAVE_DBG(slave, 1, "DC 32 bit system time offset calculation:"
            " system_time=%u (corrected with %u),"
            " app_time=%llu, diff=%i\n",
            system_time32, correction,
            slave->master->app_time, time_diff);

    if (EC_ABS(time_diff) > EC_SYSTEM_TIME_TOLERANCE_NS) {
        new_offset = time_diff + old_offset32;
        EC_SLAVE_DBG(slave, 1, "Setting time offset to %u (was %u)\n",
                new_offset, old_offset32);
        return (u64) new_offset;
    } else {
        EC_SLAVE_DBG(slave, 1, "Not touching time offset.\n");
        return old_offset;
    }
}

/****************************************************************************/

/** Configure 64 bit time offset.
 *
 * \return New offset.
 */
u64 ec_fsm_master_dc_offset64(
        ec_fsm_master_t *fsm, /**< Master state machine. */
        u64 system_time, /**< System time register. */
        u64 old_offset, /**< Time offset register. */
        unsigned long jiffies_since_read /**< Jiffies for correction. */
        )
{
    ec_slave_t *slave = fsm->slave;
    u64 new_offset, correction;
    s64 time_diff;

    // correct read system time by elapsed time since read operation
    correction = (u64) (jiffies_since_read * 1000 / HZ) * 1000000;
    system_time += correction;
    time_diff = fsm->slave->master->app_time - system_time;

    EC_SLAVE_DBG(slave, 1, "DC 64 bit system time offset calculation:"
            " system_time=%llu (corrected with %llu),"
            " app_time=%llu, diff=%lli\n",
            system_time, correction,
            slave->master->app_time, time_diff);

    if (EC_ABS(time_diff) > EC_SYSTEM_TIME_TOLERANCE_NS) {
        new_offset = time_diff + old_offset;
        EC_SLAVE_DBG(slave, 1, "Setting time offset to %llu (was %llu)\n",
                new_offset, old_offset);
    } else {
        new_offset = old_offset;
        EC_SLAVE_DBG(slave, 1, "Not touching time offset.\n");
    }

    return new_offset;
}

/****************************************************************************/

/** Master state: DC READ OFFSET.
 */
void ec_fsm_master_state_dc_read_offset(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;
    u64 system_time, old_offset, new_offset;
    unsigned long jiffies_since_read;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        EC_SLAVE_ERR(slave, "Failed to receive DC times datagram: ");
        ec_datagram_print_state(datagram);
        fsm->slave++;
        ec_fsm_master_enter_write_system_times(fsm);
        return;
    }

    if (datagram->working_counter != 1) {
        EC_SLAVE_WARN(slave, "Failed to get DC times: ");
        ec_datagram_print_wc_error(datagram);
        fsm->slave++;
        ec_fsm_master_enter_write_system_times(fsm);
        return;
    }

    system_time = EC_READ_U64(datagram->data);     // 0x0910
    old_offset = EC_READ_U64(datagram->data + 16); // 0x0920
    jiffies_since_read = jiffies - datagram->jiffies_sent;

    if (slave->base_dc_range == EC_DC_32) {
        new_offset = ec_fsm_master_dc_offset32(fsm,
                system_time, old_offset, jiffies_since_read);
    } else {
        new_offset = ec_fsm_master_dc_offset64(fsm,
                system_time, old_offset, jiffies_since_read);
    }

    // set DC system time offset and transmission delay
    ec_datagram_fpwr(datagram, slave->station_address, 0x0920, 12);
    EC_WRITE_U64(datagram->data, new_offset);
    EC_WRITE_U32(datagram->data + 8, slave->transmission_delay);
    fsm->datagram->device_index = slave->device_index;
    fsm->retries = EC_FSM_RETRIES;
    fsm->state = ec_fsm_master_state_dc_write_offset;
}

/****************************************************************************/

/** Master state: DC WRITE OFFSET.
 */
void ec_fsm_master_state_dc_write_offset(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_slave_t *slave = fsm->slave;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        EC_SLAVE_ERR(slave,
                "Failed to receive DC system time offset datagram: ");
        ec_datagram_print_state(datagram);
        fsm->slave++;
        ec_fsm_master_enter_write_system_times(fsm);
        return;
    }

    if (datagram->working_counter != 1) {
        EC_SLAVE_ERR(slave, "Failed to set DC system time offset: ");
        ec_datagram_print_wc_error(datagram);
        fsm->slave++;
        ec_fsm_master_enter_write_system_times(fsm);
        return;
    }

    fsm->slave++;
    ec_fsm_master_enter_write_system_times(fsm);
}

/****************************************************************************/

/** Master state: ASSIGN SII.
 */
void ec_fsm_master_state_assign_sii(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_datagram_t *datagram = fsm->datagram;
    ec_sii_write_request_t *request = fsm->sii_request;
    ec_slave_t *slave = request->slave;

    if (datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--)
        return;

    if (datagram->state != EC_DATAGRAM_RECEIVED) {
        EC_SLAVE_ERR(slave, "Failed to receive SII assignment datagram: ");
        ec_datagram_print_state(datagram);
        goto cont;
    }

    if (datagram->working_counter != 1) {
        EC_SLAVE_ERR(slave, "Failed to assign SII back to EtherCAT: ");
        ec_datagram_print_wc_error(datagram);
        goto cont;
    }

cont:
    // found pending SII write operation. execute it!
    EC_SLAVE_DBG(slave, 1, "Writing SII data (after assignment)...\n");
    fsm->sii_index = 0;
    ec_fsm_sii_write(&fsm->fsm_sii, slave, request->offset,
            request->words, EC_FSM_SII_USE_CONFIGURED_ADDRESS);
    fsm->state = ec_fsm_master_state_write_sii;
    fsm->state(fsm); // execute immediately
}

/****************************************************************************/

/** Master state: WRITE SII.
 */
void ec_fsm_master_state_write_sii(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_master_t *master = fsm->master;
    ec_sii_write_request_t *request = fsm->sii_request;
    ec_slave_t *slave = request->slave;

    if (ec_fsm_sii_exec(&fsm->fsm_sii, fsm->datagram)) return;

    if (!ec_fsm_sii_success(&fsm->fsm_sii)) {
        EC_SLAVE_ERR(slave, "Failed to write SII data.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&master->request_queue);
        ec_fsm_master_restart(fsm);
        return;
    }

    fsm->sii_index++;
    if (fsm->sii_index < request->nwords) {
        ec_fsm_sii_write(&fsm->fsm_sii, slave,
                request->offset + fsm->sii_index,
                request->words + fsm->sii_index,
                EC_FSM_SII_USE_CONFIGURED_ADDRESS);
        ec_fsm_sii_exec(&fsm->fsm_sii, fsm->datagram); // execute immediately
        return;
    }

    // finished writing SII
    EC_SLAVE_DBG(slave, 1, "Finished writing %zu words of SII data.\n",
            request->nwords);

    if (request->offset <= 4 && request->offset + request->nwords > 4) {
        // alias was written
        slave->sii.alias = EC_READ_U16(request->words + 4);
        // TODO: read alias from register 0x0012
        slave->effective_alias = slave->sii.alias;
    }
    // TODO: Evaluate other SII contents!

    request->state = EC_INT_REQUEST_SUCCESS;
    wake_up_all(&master->request_queue);

    // check for another SII write request
    if (ec_fsm_master_action_process_sii(fsm))
        return; // processing another request

    ec_fsm_master_restart(fsm);
}

/****************************************************************************/

/** Master state: SDO DICTIONARY.
 */
void ec_fsm_master_state_sdo_dictionary(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_master_t *master = fsm->master;

    if (ec_fsm_coe_exec(&fsm->fsm_coe, fsm->datagram)) {
        return;
    }

    if (!ec_fsm_coe_success(&fsm->fsm_coe)) {
        ec_fsm_master_restart(fsm);
        return;
    }

    // SDO dictionary fetching finished

    if (master->debug_level) {
        unsigned int sdo_count, entry_count;
        ec_slave_sdo_dict_info(slave, &sdo_count, &entry_count);
        EC_SLAVE_DBG(slave, 1, "Fetched %u SDOs and %u entries.\n",
               sdo_count, entry_count);
    }

    // attach pdo names from dictionary
    ec_slave_attach_pdo_names(slave);

    // dictionary is now complete; accept external requests for this slave.
    ec_fsm_slave_set_ready(&slave->fsm);

    ec_fsm_master_restart(fsm);
}

/****************************************************************************/

/** Master state: SDO REQUEST.
 */
void ec_fsm_master_state_sdo_request(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_sdo_request_t *request = fsm->sdo_request;

    if (!request) {
        // configuration was cleared in the meantime
        ec_fsm_master_restart(fsm);
        return;
    }

    if (ec_fsm_coe_exec(&fsm->fsm_coe, fsm->datagram)) {
        return;
    }

    if (!ec_fsm_coe_success(&fsm->fsm_coe)) {
        EC_SLAVE_DBG(fsm->slave, 1,
                "Failed to process internal SDO request.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&fsm->master->request_queue);
        ec_fsm_master_restart(fsm);
        return;
    }

    // SDO request finished
    request->state = EC_INT_REQUEST_SUCCESS;
    wake_up_all(&fsm->master->request_queue);

    EC_SLAVE_DBG(fsm->slave, 1, "Finished internal SDO request.\n");

    // check for another SDO/SoE request
    if (ec_fsm_master_action_process_int_request(fsm)) {
        return; // processing another request
    }

    ec_fsm_master_restart(fsm);
}

/****************************************************************************/

/** Master state: SoE REQUEST.
 */
void ec_fsm_master_state_soe_request(
        ec_fsm_master_t *fsm /**< Master state machine. */
        )
{
    ec_soe_request_t *request = fsm->soe_request;

    if (!request) {
        // configuration was cleared in the meantime
        ec_fsm_master_restart(fsm);
        return;
    }

    if (ec_fsm_soe_exec(&fsm->fsm_soe, fsm->datagram)) {
        return;
    }

    if (!ec_fsm_soe_success(&fsm->fsm_soe)) {
        EC_SLAVE_DBG(fsm->slave, 1,
                "Failed to process internal SoE request.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&fsm->master->request_queue);
        ec_fsm_master_restart(fsm);
        return;
    }

    // SoE request finished
    request->state = EC_INT_REQUEST_SUCCESS;
    wake_up_all(&fsm->master->request_queue);

    EC_SLAVE_DBG(fsm->slave, 1, "Finished internal SoE request.\n");

    // check for another CoE/SoE request
    if (ec_fsm_master_action_process_int_request(fsm)) {
        return; // processing another request
    }

    ec_fsm_master_restart(fsm);
}

/****************************************************************************/
