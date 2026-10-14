/*****************************************************************************
 *
 *  Copyright (C) 2006-2008  Florian Pose, Ingenieurgemeinschaft IgH
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
   Mailbox functionality.
*/

/****************************************************************************/

#include <linux/slab.h>
#include <linux/delay.h>

#include "mailbox.h"
#include "datagram.h"
#include "master.h"

/****************************************************************************/

/** Initialize mailbox response data.
 */
void ec_mbox_data_init(
        ec_mbox_data_t *mbox_data /**< Mailbox response data. */
        )
{
    mbox_data->data = NULL;
    mbox_data->data_size = 0;
    mbox_data->payload_size = 0;
}

/****************************************************************************/

/** Free internal memory for mailbox response data.
 */
void ec_mbox_data_clear(
        ec_mbox_data_t *mbox_data /**< Mailbox response data. */
        )
{
    if (mbox_data->data) {
        kfree(mbox_data->data);
        mbox_data->data = NULL;
        mbox_data->data_size = 0;
    }
}

/****************************************************************************/

/** Allocates internal memory for mailbox response data.
 *
 * \return 0 in case of success, otherwise \a -ENOMEM.
 */
static int ec_mbox_data_prealloc(
        ec_mbox_data_t *mbox_data, /**< Mailbox response data. */
        size_t size /**< Mailbox size in bytes. */
        )
{
    if (mbox_data->data) {
        // never re-allocate, the receive path may be writing to the buffer
        return 0;
    }

    if (!(mbox_data->data = kmalloc(size, GFP_KERNEL))) {
        EC_ERR("Failed to allocate %zu bytes of mailbox data memory!\n",
                size);
        return -ENOMEM;
    }
    mbox_data->data_size = size;
    return 0;
}

/****************************************************************************/

/** Allocates the mailbox response buffers of a slave.
 *
 * The buffers are allocated for the mailbox protocols supported by the slave.
 * They are allocated only once and freed with the slave, because the receive
 * path may write to them at any time. So they are dimensioned for the
 * largest TX mailbox, that can be configured during the lifetime of the
 * slave object (standard and bootstrap mailbox, sync manager configuration
 * from the SII and the currently configured mailbox).
 */
void ec_slave_mbox_prealloc(
        ec_slave_t *slave /**< EtherCAT slave. */
        )
{
    uint16_t protocols = slave->sii.mailbox_protocols;
    size_t size = slave->configured_tx_mailbox_size;

    size = max_t(size_t, size, slave->sii.std_tx_mailbox_size);
    size = max_t(size_t, size, slave->sii.boot_tx_mailbox_size);
    if (slave->sii.sync_count >= 2) {
        size = max_t(size_t, size, slave->sii.syncs[1].default_length);
    }

    if (!size || size > EC_MAX_DATA_SIZE) {
        size = EC_MAX_DATA_SIZE;
    }

#ifdef EC_EOE
    if (protocols & EC_MBOX_EOE) {
        ec_mbox_data_prealloc(&slave->mbox_eoe_data, size);
    }
#endif
    if (protocols & EC_MBOX_COE) {
        ec_mbox_data_prealloc(&slave->mbox_coe_data, size);
    }
    if (protocols & EC_MBOX_FOE) {
        ec_mbox_data_prealloc(&slave->mbox_foe_data, size);
    }
    if (protocols & EC_MBOX_SOE) {
        ec_mbox_data_prealloc(&slave->mbox_soe_data, size);
    }
    if (protocols & EC_MBOX_VOE) {
        ec_mbox_data_prealloc(&slave->mbox_voe_data, size);
    }
}

/****************************************************************************/

/**
   Prepares a mailbox-send datagram.
   \return Pointer to mailbox datagram data, or ERR_PTR() code.
*/

uint8_t *ec_slave_mbox_prepare_send(const ec_slave_t *slave, /**< slave */
                                    ec_datagram_t *datagram, /**< datagram */
                                    uint8_t type, /**< mailbox protocol */
                                    size_t size /**< size of the data */
                                    )
{
    size_t total_size;
    int ret;

    if (unlikely(!slave->sii.mailbox_protocols)) {
        EC_SLAVE_ERR(slave, "Slave does not support mailbox"
                " communication!\n");
        return ERR_PTR(-EPROTONOSUPPORT);
    }

    total_size = EC_MBOX_HEADER_SIZE + size;

    if (unlikely(total_size > slave->configured_rx_mailbox_size)) {
        EC_SLAVE_ERR(slave, "Data size (%zu) does not fit in mailbox (%u)!\n",
                total_size, slave->configured_rx_mailbox_size);
        return ERR_PTR(-ENOBUFS);
    }

    ret = ec_datagram_fpwr(datagram, slave->station_address,
            slave->configured_rx_mailbox_offset,
            slave->configured_rx_mailbox_size);
    if (ret)
        return ERR_PTR(ret);

    EC_WRITE_U16(datagram->data,     size); // mailbox service data length
    EC_WRITE_U16(datagram->data + 2, slave->station_address); // station addr.
    EC_WRITE_U8 (datagram->data + 4, 0x00); // channel & priority
    EC_WRITE_U8 (datagram->data + 5, type); // underlying protocol type

    return datagram->data + EC_MBOX_HEADER_SIZE;
}

/****************************************************************************/

/**
   Prepares a datagram for checking the mailbox state.
   \todo Determine sync manager used for receive mailbox
   \return 0 in case of success, else < 0
*/

int ec_slave_mbox_prepare_check(const ec_slave_t *slave, /**< slave */
                                ec_datagram_t *datagram /**< datagram */
                                )
{
    int ret = ec_datagram_fprd(datagram, slave->station_address, 0x808, 8);
    if (ret)
        return ret;

    ec_datagram_zero(datagram);
    return 0;
}

/****************************************************************************/

/**
   Processes a mailbox state checking datagram.
   \return 0 in case of success, else < 0
*/

int ec_slave_mbox_check(const ec_datagram_t *datagram /**< datagram */)
{
    return EC_READ_U8(datagram->data + 5) & 8 ? 1 : 0;
}

/****************************************************************************/

/**
   Prepares a datagram to fetch mailbox data.
   \return 0 in case of success, else < 0
*/

int ec_slave_mbox_prepare_fetch(const ec_slave_t *slave, /**< slave */
                                ec_datagram_t *datagram /**< datagram */
                                )
{
    int ret = ec_datagram_fprd(datagram, slave->station_address,
            slave->configured_tx_mailbox_offset,
            slave->configured_tx_mailbox_size);
    if (ret)
        return ret;

    ec_datagram_zero(datagram);
    return 0;
}

/****************************************************************************/

/**
   Takes over a mailbox error response from a mailbox fetch datagram.

   Mailbox error responses (type 0x00) can not be assigned to a protocol, so
   the mailbox dispatcher leaves them in the memory of the fetch datagram.
   This function stores such a response in the protocol buffer of the
   fetching state machine, so that ec_slave_mbox_fetch() reports the error.

   The fetch datagram is zeroed in ec_slave_mbox_prepare_fetch(), so the
   length field is zero, if the response was dispatched to a protocol buffer.
*/
void ec_slave_mbox_take_error(
        ec_mbox_data_t *mbox_data, /**< protocol buffer of the fetching FSM */
        const ec_datagram_t *datagram /**< mailbox fetch datagram */
        )
{
    if (datagram->working_counter != 1 ||
            datagram->data_size < EC_MBOX_HEADER_SIZE ||
            !EC_READ_U16(datagram->data) || // length
            (EC_READ_U8(datagram->data + 5) & 0x0F) != 0x00) { // type
        return;
    }

    if (mbox_data->data && datagram->data_size <= mbox_data->data_size) {
        memcpy(mbox_data->data, datagram->data, datagram->data_size);
        smp_store_release(&mbox_data->payload_size, datagram->data_size);
    }
}

/****************************************************************************/

/**
   Mailbox error codes.
*/

const ec_code_msg_t mbox_error_messages[] = {
    {0x00000001, "MBXERR_SYNTAX"},
    {0x00000002, "MBXERR_UNSUPPORTEDPROTOCOL"},
    {0x00000003, "MBXERR_INVAILDCHANNEL"},
    {0x00000004, "MBXERR_SERVICENOTSUPPORTED"},
    {0x00000005, "MBXERR_INVALIDHEADER"},
    {0x00000006, "MBXERR_SIZETOOSHORT"},
    {0x00000007, "MBXERR_NOMOREMEMORY"},
    {0x00000008, "MBXERR_INVALIDSIZE"},
    {}
};

/****************************************************************************/

/** Processes received mailbox data.
 *
 * The response is consumed, i. e. the response buffer is marked as empty.
 *
 * \return Pointer to the received data, or ERR_PTR() code.
 */
uint8_t *ec_slave_mbox_fetch(const ec_slave_t *slave, /**< slave */
                             ec_mbox_data_t *response_data, /**< response */
                             uint8_t *type, /**< expected mailbox protocol */
                             size_t *size /**< size of the received data */
                             )
{
    size_t data_size;

    if (!response_data->data) {
        EC_SLAVE_ERR(slave, "No mailbox response data received!\n");
        return ERR_PTR(-EPROTO);
    }

    response_data->payload_size = 0;
    data_size = EC_READ_U16(response_data->data);

    if (data_size + EC_MBOX_HEADER_SIZE > slave->configured_tx_mailbox_size) {
        EC_SLAVE_ERR(slave, "Corrupt mailbox response received!\n");
        ec_print_data(response_data->data, slave->configured_tx_mailbox_size);
        return ERR_PTR(-EPROTO);
    }

    *type = EC_READ_U8(response_data->data + 5) & 0x0F;
    *size = data_size;

    if (*type == 0x00) {
        const ec_code_msg_t *mbox_msg;
        uint16_t code = EC_READ_U16(response_data->data + 8);

        EC_SLAVE_ERR(slave, "Mailbox error response received - ");

        for (mbox_msg = mbox_error_messages; mbox_msg->code; mbox_msg++) {
            if (mbox_msg->code != code)
                continue;
            printk(KERN_CONT "Code 0x%04X: \"%s\".\n",
                    mbox_msg->code, mbox_msg->message);
            break;
        }

        if (!mbox_msg->code) {
            printk(KERN_CONT "Unknown error reply code 0x%04X.\n", code);
        }

        if (slave->master->debug_level)
            ec_print_data(response_data->data + EC_MBOX_HEADER_SIZE,
                    data_size);

        return ERR_PTR(-EPROTO);
    }

    return response_data->data + EC_MBOX_HEADER_SIZE;
}

/****************************************************************************/
