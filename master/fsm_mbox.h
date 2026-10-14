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

#ifndef __EC_FSM_MBOX_H__
#define __EC_FSM_MBOX_H__

#include "globals.h"
#include "datagram.h"
#include "mailbox.h"

/****************************************************************************/

/** Mode of the mailbox receive state machine.
 */
typedef enum {
    EC_FSM_MBOX_WAIT, /**< Wait for a response until the timeout elapsed. */
    EC_FSM_MBOX_POLL, /**< Check the mailbox once, do not wait. */
    EC_FSM_MBOX_NOSYNC /**< Fetch the mailbox once without checking it. */
} ec_fsm_mbox_mode_t;

/****************************************************************************/

typedef struct ec_fsm_mbox ec_fsm_mbox_t; /**< \see ec_fsm_mbox */

/** EtherCAT mailbox receive state machine.
 *
 * Checks the slave's TX mailbox and fetches its contents, until a message
 * for the protocol in question is stored in the given mailbox response
 * buffer. The messages are distributed to the protocol buffers by the
 * datagram receive path (see ec_master_mbox_dispatch()), so messages of
 * other protocols, which are fetched in the meantime, are not lost.
 *
 * The slave's mailbox read lock serializes the mailbox checks and fetches of
 * the different protocol state machines. If another state machine holds the
 * lock, the datagram is not used and the state machine waits for the lock
 * or the response.
 */
struct ec_fsm_mbox {
    ec_slave_t *slave; /**< Slave the FSM runs on. */
    ec_mbox_data_t *mbox_data; /**< Response buffer of the protocol. */
    ec_fsm_mbox_mode_t mode; /**< Receive mode. */
    unsigned long jiffies_start; /**< Reference time for the timeout. */
    unsigned long timeout_ms; /**< Response timeout in [ms]. */
    void (*state)(ec_fsm_mbox_t *, ec_datagram_t *); /**< State function. */
    ec_datagram_t *datagram; /**< Datagram used in the last step. */
    unsigned int retries; /**< Retries upon datagram timeout. */
    int result; /**< Zero on success, otherwise a negative error code. */
};

/****************************************************************************/

void ec_fsm_mbox_init(ec_fsm_mbox_t *);
void ec_fsm_mbox_clear(ec_fsm_mbox_t *);

void ec_fsm_mbox_receive(ec_fsm_mbox_t *, ec_slave_t *, ec_mbox_data_t *,
        unsigned long, unsigned long);
void ec_fsm_mbox_poll(ec_fsm_mbox_t *, ec_slave_t *, ec_mbox_data_t *);
void ec_fsm_mbox_fetch_nosync(ec_fsm_mbox_t *, ec_slave_t *,
        ec_mbox_data_t *, unsigned long, unsigned long);

int ec_fsm_mbox_exec(ec_fsm_mbox_t *, ec_datagram_t *);
int ec_fsm_mbox_result(const ec_fsm_mbox_t *);
int ec_fsm_mbox_holds_lock(const ec_fsm_mbox_t *);
void ec_fsm_mbox_abort(ec_fsm_mbox_t *);

/****************************************************************************/

#endif
