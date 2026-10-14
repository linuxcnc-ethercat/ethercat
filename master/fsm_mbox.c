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

/** \file
 * EtherCAT mailbox receive state machine.
 */

/****************************************************************************/

#include "globals.h"
#include "master.h"
#include "slave.h"
#include "fsm_mbox.h"

/****************************************************************************/

void ec_fsm_mbox_state_start(ec_fsm_mbox_t *, ec_datagram_t *);
void ec_fsm_mbox_state_check(ec_fsm_mbox_t *, ec_datagram_t *);
void ec_fsm_mbox_state_fetch(ec_fsm_mbox_t *, ec_datagram_t *);
void ec_fsm_mbox_state_end(ec_fsm_mbox_t *, ec_datagram_t *);
void ec_fsm_mbox_state_error(ec_fsm_mbox_t *, ec_datagram_t *);

/****************************************************************************/

/** Constructor.
 */
void ec_fsm_mbox_init(
        ec_fsm_mbox_t *fsm /**< Finite state machine. */
        )
{
    fsm->slave = NULL;
    fsm->mbox_data = NULL;
    fsm->mode = EC_FSM_MBOX_WAIT;
    fsm->jiffies_start = 0;
    fsm->timeout_ms = 0;
    fsm->state = ec_fsm_mbox_state_error;
    fsm->datagram = NULL;
    fsm->retries = 0;
    fsm->result = -EINVAL;
}

/****************************************************************************/

/** Destructor.
 */
void ec_fsm_mbox_clear(
        ec_fsm_mbox_t *fsm /**< Finite state machine. */
        )
{
    ec_fsm_mbox_abort(fsm);
}

/****************************************************************************/

/** Starts the state machine in the given mode.
 */
static void ec_fsm_mbox_start(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_slave_t *slave, /**< EtherCAT slave. */
        ec_mbox_data_t *mbox_data, /**< Response buffer of the protocol. */
        ec_fsm_mbox_mode_t mode, /**< Receive mode. */
        unsigned long jiffies_start, /**< Reference time for the timeout. */
        unsigned long timeout_ms /**< Response timeout in [ms]. */
        )
{
    fsm->slave = slave;
    fsm->mbox_data = mbox_data;
    fsm->mode = mode;
    fsm->jiffies_start = jiffies_start;
    fsm->timeout_ms = timeout_ms;
    fsm->state = ec_fsm_mbox_state_start;
    fsm->datagram = NULL;
    fsm->retries = 0;
    fsm->result = 0;
}

/****************************************************************************/

/** Starts waiting for a mailbox response.
 *
 * The state machine checks the mailbox until a response for the protocol
 * is stored in \a mbox_data, or the timeout elapsed.
 */
void ec_fsm_mbox_receive(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_slave_t *slave, /**< EtherCAT slave. */
        ec_mbox_data_t *mbox_data, /**< Response buffer of the protocol. */
        unsigned long jiffies_start, /**< Reference time for the timeout,
                                       usually the time the request was
                                       sent. */
        unsigned long timeout_ms /**< Response timeout in [ms]. */
        )
{
    ec_fsm_mbox_start(fsm, slave, mbox_data, EC_FSM_MBOX_WAIT,
            jiffies_start, timeout_ms);
}

/****************************************************************************/

/** Starts a single mailbox check without waiting.
 *
 * If no message for the protocol is available, or another state machine
 * reads the mailbox, the state machine fails with -EAGAIN or -EBUSY.
 */
void ec_fsm_mbox_poll(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_slave_t *slave, /**< EtherCAT slave. */
        ec_mbox_data_t *mbox_data /**< Response buffer of the protocol. */
        )
{
    ec_fsm_mbox_start(fsm, slave, mbox_data, EC_FSM_MBOX_POLL, jiffies, 0);
}

/****************************************************************************/

/** Starts a single mailbox fetch without a preceding check.
 *
 * Waits for the mailbox read lock until the timeout elapsed. If the fetched
 * mailbox contains no message for the protocol, the state machine fails with
 * -EAGAIN.
 */
void ec_fsm_mbox_fetch_nosync(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_slave_t *slave, /**< EtherCAT slave. */
        ec_mbox_data_t *mbox_data, /**< Response buffer of the protocol. */
        unsigned long jiffies_start, /**< Reference time for the timeout. */
        unsigned long timeout_ms /**< Timeout in [ms]. */
        )
{
    ec_fsm_mbox_start(fsm, slave, mbox_data, EC_FSM_MBOX_NOSYNC,
            jiffies_start, timeout_ms);
}

/****************************************************************************/

/** Executes the current state of the state machine.
 *
 * Every state either uses the datagram, or marks it as unused.
 *
 * \return 1, if the state machine is still running, 0 if it terminated.
 *         Use ec_fsm_mbox_result() to get the result.
 */
int ec_fsm_mbox_exec(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    if (fsm->state == ec_fsm_mbox_state_end ||
            fsm->state == ec_fsm_mbox_state_error) {
        return 0;
    }

    fsm->state(fsm, datagram);

    if (fsm->state == ec_fsm_mbox_state_end ||
            fsm->state == ec_fsm_mbox_state_error) {
        fsm->datagram = NULL;
        return 0;
    }

    if (datagram->state != EC_DATAGRAM_UNUSED) {
        fsm->datagram = datagram;
    }
    return 1;
}

/****************************************************************************/

/** Returns the result of the terminated state machine.
 *
 * \retval 0 A message for the protocol is available in the response buffer.
 * \retval -EIO Mailbox check or fetch failed.
 * \retval -ETIMEDOUT No response within the timeout.
 * \retval -EAGAIN No message for the protocol (poll and nosync mode).
 * \retval -EBUSY Another state machine reads the mailbox (poll mode).
 */
int ec_fsm_mbox_result(
        const ec_fsm_mbox_t *fsm /**< Finite state machine. */
        )
{
    return fsm->result;
}

/****************************************************************************/

/** Checks, if the state machine holds the slave's mailbox read lock.
 *
 * \return Non-zero, if the lock is held.
 */
int ec_fsm_mbox_holds_lock(
        const ec_fsm_mbox_t *fsm /**< Finite state machine. */
        )
{
    return fsm->state == ec_fsm_mbox_state_check ||
        fsm->state == ec_fsm_mbox_state_fetch;
}

/****************************************************************************/

/** Aborts the state machine and releases the mailbox read lock, if held.
 */
void ec_fsm_mbox_abort(
        ec_fsm_mbox_t *fsm /**< Finite state machine. */
        )
{
    if (ec_fsm_mbox_holds_lock(fsm)) {
        ec_read_mbox_lock_clear(fsm->slave);
    }

    if (fsm->state != ec_fsm_mbox_state_end &&
            fsm->state != ec_fsm_mbox_state_error) {
        fsm->result = -ECANCELED;
        fsm->state = ec_fsm_mbox_state_error;
    }
    fsm->datagram = NULL;
}

/****************************************************************************/

/** Checks, if a message for the protocol is available.
 *
 * \return Non-zero, if the response buffer contains a message.
 */
static int ec_fsm_mbox_data_available(
        const ec_fsm_mbox_t *fsm /**< Finite state machine. */
        )
{
    return smp_load_acquire(&fsm->mbox_data->payload_size) > 0;
}

/****************************************************************************/

/** Checks, if the timeout elapsed.
 *
 * \return Non-zero, if the timeout elapsed.
 */
static int ec_fsm_mbox_timed_out(
        const ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        unsigned long now /**< Current time in jiffies. */
        )
{
    return (now - fsm->jiffies_start) * 1000 / HZ >= fsm->timeout_ms;
}

/****************************************************************************/

/** Releases the mailbox read lock and terminates with an error.
 */
static void ec_fsm_mbox_fail(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_datagram_t *datagram, /**< Datagram to use. */
        int result /**< Negative error code. */
        )
{
    ec_read_mbox_lock_clear(fsm->slave);
    datagram->state = EC_DATAGRAM_UNUSED;
    fsm->result = result;
    fsm->state = ec_fsm_mbox_state_error;
}

/****************************************************************************/

/** State: START.
 *
 * Takes the mailbox read lock and checks (or fetches) the mailbox. If
 * another state machine holds the lock, waits for the lock or the response.
 */
void ec_fsm_mbox_state_start(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;

    if (ec_fsm_mbox_data_available(fsm)) {
        // data were already received by another read request
        datagram->state = EC_DATAGRAM_UNUSED;
        fsm->state = ec_fsm_mbox_state_end;
        return;
    }

    if (ec_read_mbox_locked(slave)) {
        // another state machine reads the mailbox
        datagram->state = EC_DATAGRAM_UNUSED;

        if (fsm->mode == EC_FSM_MBOX_POLL) {
            fsm->result = -EBUSY;
            fsm->state = ec_fsm_mbox_state_error;
        } else if (ec_fsm_mbox_timed_out(fsm, jiffies)) {
            fsm->result = -ETIMEDOUT;
            fsm->state = ec_fsm_mbox_state_error;
        }
        return;
    }

    fsm->retries = EC_FSM_RETRIES;

    if (fsm->mode == EC_FSM_MBOX_NOSYNC) {
        ec_slave_mbox_prepare_fetch(slave, datagram); // can not fail.
        fsm->state = ec_fsm_mbox_state_fetch;
    } else {
        ec_slave_mbox_prepare_check(slave, datagram); // can not fail.
        fsm->state = ec_fsm_mbox_state_check;
    }
}

/****************************************************************************/

/** State: CHECK.
 *
 * Evaluates the mailbox check datagram. The mailbox read lock is held.
 */
void ec_fsm_mbox_state_check(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    int verbose = fsm->mode != EC_FSM_MBOX_POLL;

    if (fsm->datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--) {
        ec_slave_mbox_prepare_check(slave, datagram); // can not fail.
        return;
    }

    if (fsm->datagram->state != EC_DATAGRAM_RECEIVED) {
        if (verbose) {
            EC_SLAVE_ERR(slave, "Failed to receive mailbox check"
                    " datagram: ");
            ec_datagram_print_state(fsm->datagram);
        }
        ec_fsm_mbox_fail(fsm, datagram, -EIO);
        return;
    }

    if (fsm->datagram->working_counter != 1) {
        if (verbose) {
            EC_SLAVE_ERR(slave, "Reception of mailbox check"
                    " datagram failed: ");
            ec_datagram_print_wc_error(fsm->datagram);
        }
        ec_fsm_mbox_fail(fsm, datagram, -EIO);
        return;
    }

    if (ec_slave_mbox_check(fsm->datagram)) {
        ec_slave_mbox_prepare_fetch(slave, datagram); // can not fail.
        fsm->retries = EC_FSM_RETRIES;
        fsm->state = ec_fsm_mbox_state_fetch;
        return;
    }

    // mailbox empty

    if (ec_fsm_mbox_data_available(fsm)) {
        // data were already received by another read request
        ec_read_mbox_lock_clear(slave);
        datagram->state = EC_DATAGRAM_UNUSED;
        fsm->state = ec_fsm_mbox_state_end;
        return;
    }

    if (fsm->mode == EC_FSM_MBOX_POLL) {
        ec_fsm_mbox_fail(fsm, datagram, -EAGAIN);
        return;
    }

    if (ec_fsm_mbox_timed_out(fsm, fsm->datagram->jiffies_received)) {
        ec_fsm_mbox_fail(fsm, datagram, -ETIMEDOUT);
        return;
    }

    ec_slave_mbox_prepare_check(slave, datagram); // can not fail.
    fsm->retries = EC_FSM_RETRIES;
}

/****************************************************************************/

/** State: FETCH.
 *
 * Evaluates the mailbox fetch datagram. The receive path already stored the
 * message in the response buffer of its protocol. The mailbox read lock is
 * held.
 */
void ec_fsm_mbox_state_fetch(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
    ec_slave_t *slave = fsm->slave;
    int verbose = fsm->mode != EC_FSM_MBOX_POLL;

    if (fsm->datagram->state == EC_DATAGRAM_TIMED_OUT && fsm->retries--) {
        ec_slave_mbox_prepare_fetch(slave, datagram); // can not fail.
        return;
    }

    if (fsm->datagram->state != EC_DATAGRAM_RECEIVED) {
        if (verbose) {
            EC_SLAVE_ERR(slave, "Failed to receive mailbox fetch"
                    " datagram: ");
            ec_datagram_print_state(fsm->datagram);
        }
        ec_fsm_mbox_fail(fsm, datagram, -EIO);
        return;
    }

    // a mailbox error response is not dispatched by the receive path
    ec_slave_mbox_take_error(fsm->mbox_data, fsm->datagram);

    ec_read_mbox_lock_clear(slave);
    datagram->state = EC_DATAGRAM_UNUSED;

    if (ec_fsm_mbox_data_available(fsm)) {
        fsm->state = ec_fsm_mbox_state_end;
        return;
    }

    if (fsm->mode == EC_FSM_MBOX_NOSYNC) {
        // no data in the mailbox, or a message of another protocol
        fsm->result = -EAGAIN;
        fsm->state = ec_fsm_mbox_state_error;
        return;
    }

    if (fsm->datagram->working_counter != 1) {
        if (verbose) {
            EC_SLAVE_ERR(slave, "Reception of mailbox fetch"
                    " datagram failed: ");
            ec_datagram_print_wc_error(fsm->datagram);
        }
        fsm->result = -EIO;
        fsm->state = ec_fsm_mbox_state_error;
        return;
    }

    if (fsm->mode == EC_FSM_MBOX_POLL) {
        // message of another protocol
        fsm->result = -EAGAIN;
        fsm->state = ec_fsm_mbox_state_error;
        return;
    }

    // message of another protocol: continue waiting
    fsm->state = ec_fsm_mbox_state_start;
    fsm->state(fsm, datagram);
}

/****************************************************************************/

/** State: END.
 */
void ec_fsm_mbox_state_end(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
}

/****************************************************************************/

/** State: ERROR.
 */
void ec_fsm_mbox_state_error(
        ec_fsm_mbox_t *fsm, /**< Finite state machine. */
        ec_datagram_t *datagram /**< Datagram to use. */
        )
{
}

/****************************************************************************/
