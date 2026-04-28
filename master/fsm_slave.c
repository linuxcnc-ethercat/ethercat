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
 * EtherCAT slave (SDO) state machine.
 */

/****************************************************************************/

#include "globals.h"
#include "master.h"
#include "mailbox.h"
#include "slave_config.h"

#include "fsm_slave.h"

/****************************************************************************/

void ec_fsm_slave_state_idle(ec_fsm_slave_t *, ec_datagram_t *);
void ec_fsm_slave_state_ready(ec_fsm_slave_t *, ec_datagram_t *);
int ec_fsm_slave_action_scan(ec_fsm_slave_t *, ec_datagram_t *);
void ec_fsm_slave_state_scan(ec_fsm_slave_t *, ec_datagram_t *);
void ec_fsm_slave_state_acknowledge(ec_fsm_slave_t *, ec_datagram_t *);
void ec_fsm_slave_state_config(ec_fsm_slave_t *, ec_datagram_t *);
int ec_fsm_slave_action_process_sdo(ec_fsm_slave_t *, ec_datagram_t *);
void ec_fsm_slave_state_sdo_request(ec_fsm_slave_t *, ec_datagram_t *);
int ec_fsm_slave_action_process_reg(ec_fsm_slave_t *, ec_datagram_t *);
void ec_fsm_slave_state_reg_request(ec_fsm_slave_t *, ec_datagram_t *);
int ec_fsm_slave_action_process_foe(ec_fsm_slave_t *, ec_datagram_t *);
void ec_fsm_slave_state_foe_request(ec_fsm_slave_t *, ec_datagram_t *);
int ec_fsm_slave_action_process_soe(ec_fsm_slave_t *, ec_datagram_t *);
void ec_fsm_slave_state_soe_request(ec_fsm_slave_t *, ec_datagram_t *);
#ifdef EC_EOE
int ec_fsm_slave_action_process_eoe(ec_fsm_slave_t *, ec_datagram_t *);
void ec_fsm_slave_state_eoe_request(ec_fsm_slave_t *, ec_datagram_t *);
#endif

/****************************************************************************/

/** Constructor.
 */
void ec_fsm_slave_init(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_slave_t *slave /**< EtherCAT slave. */
        )
{
    fsm->slave = slave;
    INIT_LIST_HEAD(&fsm->list); // mark as unlisted

    fsm->state = ec_fsm_slave_state_idle;
    fsm->datagram = NULL;
    fsm->sdo_request = NULL;
    fsm->reg_request = NULL;
    fsm->foe_request = NULL;
    fsm->soe_request = NULL;
#ifdef EC_EOE
    fsm->eoe_request = NULL;
#endif

    // Init sub-state-machines
    ec_fsm_coe_init(&fsm->fsm_coe);
    ec_fsm_foe_init(&fsm->fsm_foe);
    ec_fsm_soe_init(&fsm->fsm_soe);
#ifdef EC_EOE
    ec_fsm_eoe_init(&fsm->fsm_eoe);
    ec_fsm_change_init(&fsm->fsm_change);
    ec_fsm_pdo_init(&fsm->fsm_pdo, &fsm->fsm_coe);
    ec_fsm_slave_config_init(&fsm->fsm_slave_config, slave, &fsm->fsm_change,
            &fsm->fsm_coe, &fsm->fsm_soe, &fsm->fsm_pdo, &fsm->fsm_eoe);
#else
    ec_fsm_change_init(&fsm->fsm_change);
    ec_fsm_pdo_init(&fsm->fsm_pdo, &fsm->fsm_coe);
    ec_fsm_slave_config_init(&fsm->fsm_slave_config, slave, &fsm->fsm_change,
            &fsm->fsm_coe, &fsm->fsm_soe, &fsm->fsm_pdo, NULL);
#endif
    ec_fsm_slave_scan_init(&fsm->fsm_slave_scan, slave,
            &fsm->fsm_slave_config, &fsm->fsm_pdo);
}

/****************************************************************************/

/** Destructor.
 */
void ec_fsm_slave_clear(
        ec_fsm_slave_t *fsm /**< Master state machine. */
        )
{
    // signal requests that are currently in operation

    if (fsm->sdo_request) {
        fsm->sdo_request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&fsm->slave->master->request_queue);
    }

    if (fsm->reg_request) {
        fsm->reg_request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&fsm->slave->master->request_queue);
    }

    if (fsm->foe_request) {
        fsm->foe_request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&fsm->slave->master->request_queue);
    }

    if (fsm->soe_request) {
        fsm->soe_request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&fsm->slave->master->request_queue);
    }

#ifdef EC_EOE
    if (fsm->eoe_request) {
        fsm->eoe_request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&fsm->slave->master->request_queue);
    }
#endif

    // clear sub-state machines
    ec_fsm_coe_clear(&fsm->fsm_coe);
    ec_fsm_foe_clear(&fsm->fsm_foe);
    ec_fsm_soe_clear(&fsm->fsm_soe);
#ifdef EC_EOE
    ec_fsm_eoe_clear(&fsm->fsm_eoe);
#endif
    ec_fsm_change_clear(&fsm->fsm_change);
    ec_fsm_pdo_clear(&fsm->fsm_pdo);
    ec_fsm_slave_config_clear(&fsm->fsm_slave_config);
    ec_fsm_slave_scan_clear(&fsm->fsm_slave_scan);
}

/****************************************************************************/

/** Executes the current state of the state machine.
 *
 * \return 1 if \a datagram was used, else 0.
 */
int ec_fsm_slave_exec(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< New datagram to use. */
        )
{
    int datagram_used;

    fsm->state(fsm, datagram);

    datagram_used = fsm->state != ec_fsm_slave_state_idle &&
        fsm->state != ec_fsm_slave_state_ready;

    if (datagram_used) {
        /* state_config bookkeeps its own fsm->datagram via
         * ec_fsm_slave_config_exec(); only record the ring slot here
         * if the child FSM actually consumed it. */
        if (datagram->state != EC_DATAGRAM_INVALID) {
            datagram->device_index = fsm->slave->device_index;
            fsm->datagram = datagram;
        }
    } else {
        fsm->datagram = NULL;
    }

    return datagram_used;
}

/****************************************************************************/

/** Sets the current state of the state machine to READY
 */
void ec_fsm_slave_set_ready(
        ec_fsm_slave_t *fsm /**< Slave state machine. */
        )
{
    if (fsm->state == ec_fsm_slave_state_idle) {
        EC_SLAVE_DBG(fsm->slave, 1, "Ready for requests.\n");
        fsm->state = ec_fsm_slave_state_ready;
    }
}

/****************************************************************************/

/** Returns, if the FSM is currently not busy and ready to execute.
 *
 * \return Non-zero if ready.
 */
int ec_fsm_slave_is_ready(
        const ec_fsm_slave_t *fsm /**< Slave state machine. */
        )
{
    return fsm->state == ec_fsm_slave_state_ready;
}

/****************************************************************************/

/** Returns non-zero when the FSM has something to do (request, config or
 * is actively running a sub-FSM) and therefore wants a ring slot.
 */
int ec_fsm_slave_has_work(
        const ec_fsm_slave_t *fsm /**< Slave state machine. */
        )
{
    return fsm->state != ec_fsm_slave_state_idle;
}

/****************************************************************************/

/** Check for pending scan.
 *
 * \return non-zero, if scan was started.
 */
int ec_fsm_slave_action_scan(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;

    if (!slave->scan_required) {
        return 0;
    }

    EC_SLAVE_DBG(slave, 1, "Scanning slave %u on %s link.\n",
            slave->ring_position,
            ec_device_names[slave->device_index != 0]);
    fsm->state = ec_fsm_slave_state_scan;
    ec_fsm_slave_scan_start(&fsm->fsm_slave_scan);
    ec_fsm_slave_scan_exec(&fsm->fsm_slave_scan, datagram); // execute immediately
    return 1;
}

/****************************************************************************/

/** Slave state: SCAN.
 */
void ec_fsm_slave_state_scan(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;

    if (ec_fsm_slave_scan_exec(&fsm->fsm_slave_scan, datagram)) {
        return;
    }

#ifdef EC_EOE
    if (slave->sii.mailbox_protocols & EC_MBOX_EOE) {
        ec_master_t *master = slave->master;
        ec_eoe_t *eoe;
        if (!(eoe = kmalloc(sizeof(ec_eoe_t), GFP_KERNEL))) {
            EC_SLAVE_ERR(slave, "Failed to allocate EoE handler memory!\n");
        } else if (ec_eoe_init(eoe, slave)) {
            EC_SLAVE_ERR(slave, "Failed to init EoE handler!\n");
            kfree(eoe);
        } else {
            list_add_tail(&eoe->list, &master->eoe_handlers);
        }
    }
#endif

    // go idle and wait for the master FSM to finish scanning before
    // starting configuration.
    slave->scan_required = 0;
    fsm->state = ec_fsm_slave_state_idle;
}

/****************************************************************************/

/** Kick the per-slave configuration FSM into motion.
 *
 * Called by the master-side FSM when it detects a slave whose current
 * state diverges from the requested one. The actual configuration then
 * runs in parallel with other slaves on the external-datagram ring.
 */
void ec_fsm_slave_start_config(
        ec_fsm_slave_t *fsm /**< Slave state machine. */
        )
{
    if (fsm->state == ec_fsm_slave_state_config) {
        return; // already running
    }
    /* If the slave is still carrying an AL error bit, park the FSM in
     * state_ready so its next tick can acknowledge the error via the
     * per-slave fsm_change. Starting state_config right now would make
     * fsm_slave_config write the first state change before the ack,
     * which the slave answers with AL code 0x001E and friends. */
    if (fsm->slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
        fsm->state = ec_fsm_slave_state_ready;
        fsm->datagram = NULL;
        return;
    }
    ec_fsm_slave_config_start(&fsm->fsm_slave_config);
    fsm->state = ec_fsm_slave_state_config;
    fsm->datagram = NULL;
}

/****************************************************************************/

/** Kick the per-slave configuration FSM into the shortened SAFEOP -> OP path.
 */
void ec_fsm_slave_start_quick_config(
        ec_fsm_slave_t *fsm /**< Slave state machine. */
        )
{
    if (fsm->state == ec_fsm_slave_state_config) {
        return; // already running
    }
    if (fsm->slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
        fsm->state = ec_fsm_slave_state_ready;
        fsm->datagram = NULL;
        return;
    }
    ec_fsm_slave_config_quick_start(&fsm->fsm_slave_config);
    fsm->state = ec_fsm_slave_state_config;
    fsm->datagram = NULL;
}

/****************************************************************************/

/** Slave state: CONFIG.
 *
 * Drives the per-slave configuration FSM until it terminates, then drops
 * back to state_ready so application requests can be serviced.
 */
void ec_fsm_slave_state_config(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    if (ec_fsm_slave_config_exec(&fsm->fsm_slave_config, datagram)) {
        return;
    }

    /* The per-state config functions already set slave->error_flag via
     * fsm_change when a transition is refused; no need to shadow it
     * here. Letting error_flag stay clear when the refusal triggered an
     * ACK_ERR lets state_ready retry on the next tick - matching the
     * Synapticon behaviour. */
    fsm->slave->force_config = 0;
    fsm->state = ec_fsm_slave_state_ready;
}

/****************************************************************************/

/** Slave state: ACKNOWLEDGE.
 *
 * Drives the per-slave fsm_change through a MODE_ACK_ONLY sequence so an
 * outstanding AL error bit can be cleared without dragging the master FSM
 * into it. Once the ack completes we drop back to state_ready; the next
 * tick will see a clean slave and start the configuration from there.
 */
void ec_fsm_slave_state_acknowledge(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;

    if (ec_fsm_change_exec(&fsm->fsm_change, datagram)) {
        return;
    }

    if (!ec_fsm_change_success(&fsm->fsm_change)) {
        slave->error_flag = 1;
        EC_SLAVE_ERR(slave, "Failed to acknowledge state change.\n");
    }

    fsm->state = ec_fsm_slave_state_ready;
}

/*****************************************************************************
 * Slave state machine
 ****************************************************************************/

/** Slave state: IDLE.
 */
void ec_fsm_slave_state_idle(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    // do nothing
}

/****************************************************************************/

/** Slave state: READY.
 */
void ec_fsm_slave_state_ready(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;

    // Check for pending bus scan first so a freshly added slave can
    // run its scan without the master FSM having to drive it.
    if (ec_fsm_slave_action_scan(fsm, datagram)) {
        return;
    }

    /* Detect a pending (re-)configuration and drive it from here so that
     * the master FSM does not have to visit every slave sequentially to
     * kick the configs - any fsm_slave that reaches state_ready picks up
     * its own work immediately and runs it in parallel with the others. */
    if (!slave->error_flag) {
        /* Acknowledge an outstanding AL error before attempting any
         * state change. Skipping this step makes the first SAFEOP
         * transition fail with AL code 0x001E / 0x0016 etc. because
         * the slave is still sitting in <state>+E. Running the ack
         * via the per-slave fsm_change (MODE_ACK_ONLY) clears the
         * ERR bit without dragging the master FSM into it. */
        if (slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
            fsm->state = ec_fsm_slave_state_acknowledge;
            ec_fsm_change_ack(&fsm->fsm_change, slave);
            fsm->state(fsm, datagram); // execute immediately
            return;
        }

        if (slave->current_state != slave->requested_state
                || slave->force_config) {
            /* If the slave just dropped to SAFEOP after a sync manager
             * watchdog timeout (AL code 0x001B) and the application
             * still wants OP, the existing configuration is presumed
             * valid and we take the SAFEOP -> OP short cut instead of
             * re-running init / SM / PDO / DC setup. */
            if (!slave->force_config
                    && slave->current_state == EC_SLAVE_STATE_SAFEOP
                    && slave->requested_state == EC_SLAVE_STATE_OP
                    && slave->last_al_error == 0x001B) {
                ec_fsm_slave_config_quick_start(&fsm->fsm_slave_config);
            } else {
                ec_fsm_slave_config_start(&fsm->fsm_slave_config);
            }
            fsm->state = ec_fsm_slave_state_config;
            /* Execute state_config immediately on the slot the scheduler
             * just handed in so the first config datagram is written
             * here. Otherwise the slot is recorded as "consumed" by
             * ec_fsm_slave_exec but holds no operation; it gets queued
             * empty and the FSM blocks the exec list until that ghost
             * frame round-trips, which can starve the slave on retry
             * after an AL ERR ack (slave 17/18/19 stuck in PREOP).
             * Matches Etherlab's ec_fsm_slave_action_config(). */
            fsm->state(fsm, datagram);
            return;
        }
    }

    // Check for pending external SDO requests
    if (ec_fsm_slave_action_process_sdo(fsm, datagram)) {
        return;
    }

    // Check for pending external register requests
    if (ec_fsm_slave_action_process_reg(fsm, datagram)) {
        return;
    }

    // Check for pending FoE requests
    if (ec_fsm_slave_action_process_foe(fsm, datagram)) {
        return;
    }

    // Check for pending SoE requests
    if (ec_fsm_slave_action_process_soe(fsm, datagram)) {
        return;
    }

#ifdef EC_EOE
    // Check for pending EoE IP parameter requests
    if (ec_fsm_slave_action_process_eoe(fsm, datagram)) {
        return;
    }
#endif
}

/****************************************************************************/

/** Check for pending SDO requests and process one.
 *
 * \return non-zero, if an SDO request is processed.
 */
int ec_fsm_slave_action_process_sdo(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_sdo_request_t *request;

    if (list_empty(&slave->sdo_requests)) {
        return 0;
    }

    // take the first request to be processed
    request = list_entry(slave->sdo_requests.next, ec_sdo_request_t, list);
    list_del_init(&request->list); // dequeue

    if (slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
        EC_SLAVE_WARN(slave, "Aborting SDO request,"
                " slave has error flag set.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->state = ec_fsm_slave_state_idle;
        return 0;
    }

    if (slave->current_state == EC_SLAVE_STATE_INIT) {
        EC_SLAVE_WARN(slave, "Aborting SDO request, slave is in INIT.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->state = ec_fsm_slave_state_idle;
        return 0;
    }

    fsm->sdo_request = request;
    request->state = EC_INT_REQUEST_BUSY;

    // Found pending SDO request. Execute it!
    EC_SLAVE_DBG(slave, 1, "Processing SDO request...\n");

    // Start SDO transfer
    fsm->state = ec_fsm_slave_state_sdo_request;
    ec_fsm_coe_transfer(&fsm->fsm_coe, slave, request);
    ec_fsm_coe_exec(&fsm->fsm_coe, datagram); // execute immediately
    return 1;
}

/****************************************************************************/

/** Slave state: SDO_REQUEST.
 */
void ec_fsm_slave_state_sdo_request(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_sdo_request_t *request = fsm->sdo_request;

    if (ec_fsm_coe_exec(&fsm->fsm_coe, datagram)) {
        return;
    }

    if (!ec_fsm_coe_success(&fsm->fsm_coe)) {
        EC_SLAVE_ERR(slave, "Failed to process SDO request.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->sdo_request = NULL;
        fsm->state = ec_fsm_slave_state_ready;
        return;
    }

    EC_SLAVE_DBG(slave, 1, "Finished SDO request.\n");

    // SDO request finished
    request->state = EC_INT_REQUEST_SUCCESS;
    wake_up_all(&slave->master->request_queue);
    fsm->sdo_request = NULL;
    fsm->state = ec_fsm_slave_state_ready;
}

/****************************************************************************/

/** Check for pending register requests and process one.
 *
 * \return non-zero, if a register request is processed.
 */
int ec_fsm_slave_action_process_reg(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_reg_request_t *reg;

    fsm->reg_request = NULL;

    if (slave->config) {
        // search the first internal register request to be processed
        list_for_each_entry(reg, &slave->config->reg_requests, list) {
            if (reg->state == EC_INT_REQUEST_QUEUED) {
                fsm->reg_request = reg;
                break;
            }
        }
    }

    if (!fsm->reg_request && !list_empty(&slave->reg_requests)) {
        // take the first external request to be processed
        fsm->reg_request =
            list_entry(slave->reg_requests.next, ec_reg_request_t, list);
        list_del_init(&fsm->reg_request->list); // dequeue
    }

    if (!fsm->reg_request) { // no register request to process
        return 0;
    }

    if (slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
        EC_SLAVE_WARN(slave, "Aborting register request,"
                " slave has error flag set.\n");
        fsm->reg_request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->reg_request = NULL;
        fsm->state = ec_fsm_slave_state_idle;
        return 0;
    }

    // Found pending register request. Execute it!
    EC_SLAVE_DBG(slave, 1, "Processing register request...\n");

    fsm->reg_request->state = EC_INT_REQUEST_BUSY;

    // Start register access
    if (fsm->reg_request->dir == EC_DIR_INPUT) {
        ec_datagram_fprd(datagram, slave->station_address,
                fsm->reg_request->address, fsm->reg_request->transfer_size);
        ec_datagram_zero(datagram);
    } else {
        ec_datagram_fpwr(datagram, slave->station_address,
                fsm->reg_request->address, fsm->reg_request->transfer_size);
        memcpy(datagram->data, fsm->reg_request->data,
                fsm->reg_request->transfer_size);
    }
    datagram->device_index = slave->device_index;
    fsm->state = ec_fsm_slave_state_reg_request;
    return 1;
}

/****************************************************************************/

/** Slave state: Register request.
 */
void ec_fsm_slave_state_reg_request(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_reg_request_t *reg = fsm->reg_request;

    if (!reg) {
        // configuration was cleared in the meantime
        fsm->state = ec_fsm_slave_state_ready;
        fsm->reg_request = NULL;
        return;
    }

    if (fsm->datagram->state != EC_DATAGRAM_RECEIVED) {
        EC_SLAVE_ERR(slave, "Failed to receive register"
                " request datagram: ");
        ec_datagram_print_state(fsm->datagram);
        reg->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->reg_request = NULL;
        fsm->state = ec_fsm_slave_state_ready;
        return;
    }

    if (fsm->datagram->working_counter == 1) {
        if (reg->dir == EC_DIR_INPUT) { // read request
            memcpy(reg->data, fsm->datagram->data, reg->transfer_size);
        }

        reg->state = EC_INT_REQUEST_SUCCESS;
        EC_SLAVE_DBG(slave, 1, "Register request successful.\n");
    } else {
        reg->state = EC_INT_REQUEST_FAILURE;
        ec_datagram_print_state(fsm->datagram);
        EC_SLAVE_ERR(slave, "Register request failed"
                " (working counter is %u).\n",
                fsm->datagram->working_counter);
    }

    wake_up_all(&slave->master->request_queue);
    fsm->reg_request = NULL;
    fsm->state = ec_fsm_slave_state_ready;
}

/****************************************************************************/

/** Check for pending FoE requests and process one.
 *
 * \return non-zero, if an FoE request is processed.
 */
int ec_fsm_slave_action_process_foe(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_foe_request_t *request;

    if (list_empty(&slave->foe_requests)) {
        return 0;
    }

    // take the first request to be processed
    request = list_entry(slave->foe_requests.next, ec_foe_request_t, list);
    list_del_init(&request->list); // dequeue

    if (slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
        EC_SLAVE_WARN(slave, "Aborting FoE request,"
                " slave has error flag set.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->state = ec_fsm_slave_state_idle;
        return 0;
    }

    request->state = EC_INT_REQUEST_BUSY;
    fsm->foe_request = request;

    EC_SLAVE_DBG(slave, 1, "Processing FoE request.\n");

    fsm->state = ec_fsm_slave_state_foe_request;
    ec_fsm_foe_transfer(&fsm->fsm_foe, slave, request);
    ec_fsm_foe_exec(&fsm->fsm_foe, datagram);
    return 1;
}

/****************************************************************************/

/** Slave state: FOE REQUEST.
 */
void ec_fsm_slave_state_foe_request(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_foe_request_t *request = fsm->foe_request;

    if (ec_fsm_foe_exec(&fsm->fsm_foe, datagram)) {
        return;
    }

    if (!ec_fsm_foe_success(&fsm->fsm_foe)) {
        EC_SLAVE_ERR(slave, "Failed to handle FoE request.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->foe_request = NULL;
        fsm->state = ec_fsm_slave_state_ready;
        return;
    }

    // finished transferring FoE
    EC_SLAVE_DBG(slave, 1, "Successfully transferred %zu bytes of FoE"
            " data.\n", request->data_size);

    request->state = EC_INT_REQUEST_SUCCESS;
    wake_up_all(&slave->master->request_queue);
    fsm->foe_request = NULL;
    fsm->state = ec_fsm_slave_state_ready;
}

/****************************************************************************/

/** Check for pending SoE requests and process one.
 *
 * \return non-zero, if a request is processed.
 */
int ec_fsm_slave_action_process_soe(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_soe_request_t *req;

    if (list_empty(&slave->soe_requests)) {
        return 0;
    }

    // take the first request to be processed
    req = list_entry(slave->soe_requests.next, ec_soe_request_t, list);
    list_del_init(&req->list); // dequeue

    if (slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
        EC_SLAVE_WARN(slave, "Aborting SoE request,"
                " slave has error flag set.\n");
        req->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->state = ec_fsm_slave_state_idle;
        return 0;
    }

    if (slave->current_state == EC_SLAVE_STATE_INIT) {
        EC_SLAVE_WARN(slave, "Aborting SoE request, slave is in INIT.\n");
        req->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->state = ec_fsm_slave_state_idle;
        return 0;
    }

    fsm->soe_request = req;
    req->state = EC_INT_REQUEST_BUSY;

    // Found pending request. Execute it!
    EC_SLAVE_DBG(slave, 1, "Processing SoE request...\n");

    // Start SoE transfer
    fsm->state = ec_fsm_slave_state_soe_request;
    ec_fsm_soe_transfer(&fsm->fsm_soe, slave, req);
    ec_fsm_soe_exec(&fsm->fsm_soe, datagram); // execute immediately
    return 1;
}

/****************************************************************************/

/** Slave state: SOE_REQUEST.
 */
void ec_fsm_slave_state_soe_request(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_soe_request_t *request = fsm->soe_request;

    if (ec_fsm_soe_exec(&fsm->fsm_soe, datagram)) {
        return;
    }

    if (!ec_fsm_soe_success(&fsm->fsm_soe)) {
        EC_SLAVE_ERR(slave, "Failed to process SoE request.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->soe_request = NULL;
        fsm->state = ec_fsm_slave_state_ready;
        return;
    }

    EC_SLAVE_DBG(slave, 1, "Finished SoE request.\n");

    // SoE request finished
    request->state = EC_INT_REQUEST_SUCCESS;
    wake_up_all(&slave->master->request_queue);
    fsm->soe_request = NULL;
    fsm->state = ec_fsm_slave_state_ready;
}

/****************************************************************************/

#ifdef EC_EOE
/** Check for pending EoE IP parameter requests and process one.
 *
 * \return non-zero, if a request is processed.
 */
int ec_fsm_slave_action_process_eoe(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_eoe_request_t *request;

    if (list_empty(&slave->eoe_requests)) {
        return 0;
    }

    // take the first request to be processed
    request = list_entry(slave->eoe_requests.next, ec_eoe_request_t, list);
    list_del_init(&request->list); // dequeue

    if (slave->current_state & EC_SLAVE_STATE_ACK_ERR) {
        EC_SLAVE_WARN(slave, "Aborting EoE request,"
                " slave has error flag set.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->state = ec_fsm_slave_state_idle;
        return 0;
    }

    if (slave->current_state == EC_SLAVE_STATE_INIT) {
        EC_SLAVE_WARN(slave, "Aborting EoE request, slave is in INIT.\n");
        request->state = EC_INT_REQUEST_FAILURE;
        wake_up_all(&slave->master->request_queue);
        fsm->state = ec_fsm_slave_state_idle;
        return 0;
    }

    fsm->eoe_request = request;
    request->state = EC_INT_REQUEST_BUSY;

    // Found pending request. Execute it!
    EC_SLAVE_DBG(slave, 1, "Processing EoE request...\n");

    // Start EoE command
    fsm->state = ec_fsm_slave_state_eoe_request;
    ec_fsm_eoe_set_ip_param(&fsm->fsm_eoe, slave, request);
    ec_fsm_eoe_exec(&fsm->fsm_eoe, datagram); // execute immediately
    return 1;
}

/****************************************************************************/

/** Slave state: EOE_REQUEST.
 */
void ec_fsm_slave_state_eoe_request(
        ec_fsm_slave_t *fsm, /**< Slave state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    ec_eoe_request_t *req = fsm->eoe_request;

    if (ec_fsm_eoe_exec(&fsm->fsm_eoe, datagram)) {
        return;
    }

    if (ec_fsm_eoe_success(&fsm->fsm_eoe)) {
		req->state = EC_INT_REQUEST_SUCCESS;
		EC_SLAVE_DBG(slave, 1, "Finished EoE request.\n");
    }
	else {
        req->state = EC_INT_REQUEST_FAILURE;
        EC_SLAVE_ERR(slave, "Failed to process EoE request.\n");
	}

    wake_up_all(&slave->master->request_queue);
    fsm->eoe_request = NULL;
    fsm->state = ec_fsm_slave_state_ready;
}

/****************************************************************************/
#endif
