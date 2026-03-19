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

/** \file
 * EtherCAT generic Ethernet device module.
 */

/****************************************************************************/

#include <linux/module.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/version.h>
#include <linux/if_arp.h> /* ARPHRD_ETHER */
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>

#include "../globals.h"
#include "ecdev.h"

#define PFX "ec_generic: "

#define ETH_P_ETHERCAT 0x88A4

#define EC_GEN_RX_QUEUE_MAX 128

#if defined(CONFIG_SUSE_KERNEL) && LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
#include <linux/suse_version.h>
#else
#  ifndef SUSE_VERSION
#    define SUSE_VERSION 0
#  endif
#  ifndef SUSE_PATCHLEVEL
#    define SUSE_PATCHLEVEL 0
#  endif
#endif

/****************************************************************************/

int __init ec_gen_init_module(void);
void __exit ec_gen_cleanup_module(void);
void ec_gen_poll(struct net_device *);

/****************************************************************************/

/** \cond */

MODULE_AUTHOR("Florian Pose <fp@igh.de>");
MODULE_DESCRIPTION("EtherCAT master generic Ethernet device module");
MODULE_LICENSE("GPL");
MODULE_VERSION(EC_MASTER_VERSION);

/** \endcond */

struct list_head generic_devices;

typedef struct {
    struct list_head list;
    struct net_device *netdev;
    struct net_device *used_netdev;
    ec_device_t *ecdev;
    struct sk_buff_head rx_queue;
} ec_gen_device_t;

typedef struct {
    struct list_head list;
    struct net_device *netdev;
    char name[IFNAMSIZ];
    int ifindex;
    uint8_t dev_addr[ETH_ALEN];
} ec_gen_interface_desc_t;

int ec_gen_device_init(ec_gen_device_t *);
void ec_gen_device_clear(ec_gen_device_t *);
int ec_gen_device_offer(ec_gen_device_t *, ec_gen_interface_desc_t *);
int ec_gen_device_open(ec_gen_device_t *);
int ec_gen_device_stop(ec_gen_device_t *);
int ec_gen_device_start_xmit(ec_gen_device_t *, struct sk_buff *);
void ec_gen_device_poll(ec_gen_device_t *);

int offer_device(ec_gen_interface_desc_t *);
void clear_devices(void);

/****************************************************************************/

static int ec_gen_netdev_open(struct net_device *dev)
{
    ec_gen_device_t *gendev = *((ec_gen_device_t **) netdev_priv(dev));
    return ec_gen_device_open(gendev);
}

/****************************************************************************/

static int ec_gen_netdev_stop(struct net_device *dev)
{
    ec_gen_device_t *gendev = *((ec_gen_device_t **) netdev_priv(dev));
    return ec_gen_device_stop(gendev);
}

/****************************************************************************/

static int ec_gen_netdev_start_xmit(
        struct sk_buff *skb,
        struct net_device *dev
        )
{
    ec_gen_device_t *gendev = *((ec_gen_device_t **) netdev_priv(dev));
    return ec_gen_device_start_xmit(gendev, skb);
}

/****************************************************************************/

void ec_gen_poll(struct net_device *dev)
{
    ec_gen_device_t *gendev = *((ec_gen_device_t **) netdev_priv(dev));
    ec_gen_device_poll(gendev);
}

/****************************************************************************/

static const struct net_device_ops ec_gen_netdev_ops = {
    .ndo_open       = ec_gen_netdev_open,
    .ndo_stop       = ec_gen_netdev_stop,
    .ndo_start_xmit = ec_gen_netdev_start_xmit,
};

/****************************************************************************/

/** Detach from the real network device.
 *
 * Unregisters the rx_handler and releases the dev_hold reference.
 * Called both from normal cleanup and from the netdev notifier.
 *
 * \param rtnl_held  1 if the caller already holds rtnl_lock.
 */
static void ec_gen_device_detach_real(
        ec_gen_device_t *dev,
        int rtnl_held
        )
{
    if (!dev->used_netdev)
        return;

    if (!rtnl_held)
        rtnl_lock();
    netdev_rx_handler_unregister(dev->used_netdev);
    if (!rtnl_held)
        rtnl_unlock();

    dev_put(dev->used_netdev);
    dev->used_netdev = NULL;

    skb_queue_purge(&dev->rx_queue);
}

/****************************************************************************/

/** Netdevice notifier.
 *
 * Handles NETDEV_UNREGISTER for the real device so we release our
 * references before the kernel tears down the device. This is critical
 * for clean shutdown/reboot when ethercatctl stop has not been called.
 */
static int ec_gen_netdev_event(struct notifier_block *nb,
        unsigned long event, void *ptr)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
    struct net_device *dev = netdev_notifier_info_to_dev(ptr);
#else
    struct net_device *dev = (struct net_device *)ptr;
#endif
    ec_gen_device_t *gendev;

    if (event != NETDEV_UNREGISTER)
        return NOTIFY_DONE;

    list_for_each_entry(gendev, &generic_devices, list) {
        if (gendev->used_netdev == dev) {
            printk(KERN_INFO PFX "%s going away, detaching.\n",
                    dev->name);
            ec_gen_device_detach_real(gendev, 1);
            break;
        }
    }

    return NOTIFY_DONE;
}

static struct notifier_block ec_gen_notifier = {
    .notifier_call = ec_gen_netdev_event,
};

/****************************************************************************/

/** RX handler registered on the real network device.
 *
 * Intercepts EtherCAT frames (ethertype 0x88A4) before they reach the
 * kernel protocol handlers and queues them for processing by the master's
 * poll callback. Non-EtherCAT frames pass through to the normal stack.
 */
static rx_handler_result_t ec_gen_rx_handler(struct sk_buff **pskb)
{
    struct sk_buff *skb = *pskb;
    ec_gen_device_t *gendev = rcu_dereference(skb->dev->rx_handler_data);

    if (skb->protocol == htons(ETH_P_ETHERCAT)) {
        if (skb_queue_len(&gendev->rx_queue) >= EC_GEN_RX_QUEUE_MAX) {
            kfree_skb(skb);
            return RX_HANDLER_CONSUMED;
        }
        /* Push the Ethernet header back. eth_type_trans() has already
         * pulled it, but ecdev_receive() expects data starting at the
         * Ethernet header (destination MAC). */
        skb_push(skb, ETH_HLEN);
        skb_queue_tail(&gendev->rx_queue, skb);
        return RX_HANDLER_CONSUMED;
    }

    return RX_HANDLER_PASS;
}

/****************************************************************************/

/** Init generic device.
 */
int ec_gen_device_init(
        ec_gen_device_t *dev
        )
{
    ec_gen_device_t **priv;
    char null = 0x00;

    dev->ecdev = NULL;
    dev->used_netdev = NULL;
    skb_queue_head_init(&dev->rx_queue);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 17, 0)
    dev->netdev = alloc_netdev(sizeof(ec_gen_device_t *), &null,
            NET_NAME_UNKNOWN, ether_setup);
#else
    dev->netdev = alloc_netdev(sizeof(ec_gen_device_t *), &null, ether_setup);
#endif
    if (!dev->netdev) {
        return -ENOMEM;
    }

    dev->netdev->netdev_ops = &ec_gen_netdev_ops;

    priv = netdev_priv(dev->netdev);
    *priv = dev;

    return 0;
}

/****************************************************************************/

/** Clear generic device.
 *
 * Order matters: close the master device first (stops polling), then
 * detach the rx_handler from the real device, then withdraw.
 */
void ec_gen_device_clear(
        ec_gen_device_t *dev
        )
{
    if (dev->ecdev) {
        ecdev_close(dev->ecdev);
    }
    ec_gen_device_detach_real(dev, 0);
    if (dev->ecdev) {
        ecdev_withdraw(dev->ecdev);
        dev->ecdev = NULL;
    }
    skb_queue_purge(&dev->rx_queue);
    free_netdev(dev->netdev);
}

/****************************************************************************/

/** Offer generic device to master.
 */
int ec_gen_device_offer(
        ec_gen_device_t *dev,
        ec_gen_interface_desc_t *desc
        )
{
    int ret;

    dev->used_netdev = desc->netdev;
    dev_hold(dev->used_netdev);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0) || (SUSE_VERSION == 15 && SUSE_PATCHLEVEL >= 5)
    eth_hw_addr_set(dev->netdev, desc->dev_addr);
#else
    memcpy(dev->netdev->dev_addr, desc->dev_addr, ETH_ALEN);
#endif

    dev->ecdev = ecdev_offer(dev->netdev, ec_gen_poll, THIS_MODULE);
    if (!dev->ecdev) {
        dev_put(dev->used_netdev);
        dev->used_netdev = NULL;
        return 0;
    }

    rtnl_lock();
    ret = netdev_rx_handler_register(dev->used_netdev,
            ec_gen_rx_handler, dev);
    rtnl_unlock();

    if (ret) {
        printk(KERN_ERR PFX "Failed to register rx_handler on %s"
                " (ret = %i). Device busy (bridge/bond/OVS)?\n",
                desc->name, ret);
        ecdev_withdraw(dev->ecdev);
        dev->ecdev = NULL;
        dev_put(dev->used_netdev);
        dev->used_netdev = NULL;
        return 0;
    }

    if (ecdev_open(dev->ecdev)) {
        rtnl_lock();
        netdev_rx_handler_unregister(dev->used_netdev);
        rtnl_unlock();
        ecdev_withdraw(dev->ecdev);
        dev->ecdev = NULL;
        dev_put(dev->used_netdev);
        dev->used_netdev = NULL;
        return 0;
    }

    ecdev_set_link(dev->ecdev, netif_carrier_ok(dev->used_netdev));

    return 1;
}

/****************************************************************************/

/** Open the device.
 */
int ec_gen_device_open(
        ec_gen_device_t *dev
        )
{
    return 0;
}

/****************************************************************************/

/** Stop the device.
 */
int ec_gen_device_stop(
        ec_gen_device_t *dev
        )
{
    return 0;
}

/****************************************************************************/

/** Transmit an EtherCAT frame.
 *
 * The master calls ndo_start_xmit on the virtual device with a pre-allocated
 * SKB from the TX ring. We must not consume or free that SKB, so we copy it
 * and transmit the copy through the real device.
 */
int ec_gen_device_start_xmit(
        ec_gen_device_t *dev,
        struct sk_buff *skb
        )
{
    struct sk_buff *copy;

    if (unlikely(!dev->used_netdev))
        return NETDEV_TX_BUSY;

    ecdev_set_link(dev->ecdev, netif_carrier_ok(dev->used_netdev));

    copy = skb_copy(skb, GFP_ATOMIC);
    if (unlikely(!copy))
        return NETDEV_TX_BUSY;

    copy->dev = dev->used_netdev;
    dev_queue_xmit(copy);

    return NETDEV_TX_OK;
}

/****************************************************************************/

/** Polls the device.
 *
 * Called by the master in its cyclic task. Drains all buffered EtherCAT
 * frames from the rx_queue and passes them to the master.
 */
void ec_gen_device_poll(
        ec_gen_device_t *dev
        )
{
    struct sk_buff *skb;
    int budget = 10;

    if (unlikely(!dev->used_netdev))
        return;

    ecdev_set_link(dev->ecdev, netif_carrier_ok(dev->used_netdev));

    while (budget-- && (skb = skb_dequeue(&dev->rx_queue))) {
        ecdev_receive(dev->ecdev, skb->data, skb->len);
        kfree_skb(skb);
    }
}

/****************************************************************************/

/** Offer device.
 */
int offer_device(
        ec_gen_interface_desc_t *desc
        )
{
    ec_gen_device_t *gendev;
    int ret = 0;

    gendev = kmalloc(sizeof(ec_gen_device_t), GFP_KERNEL);
    if (!gendev) {
        return -ENOMEM;
    }

    ret = ec_gen_device_init(gendev);
    if (ret) {
        kfree(gendev);
        return ret;
    }

    if (ec_gen_device_offer(gendev, desc)) {
        list_add_tail(&gendev->list, &generic_devices);
    } else {
        ec_gen_device_clear(gendev);
        kfree(gendev);
    }

    return ret;
}

/****************************************************************************/

/** Clear devices.
 */
void clear_devices(void)
{
    ec_gen_device_t *gendev, *next;

    list_for_each_entry_safe(gendev, next, &generic_devices, list) {
        list_del(&gendev->list);
        ec_gen_device_clear(gendev);
        kfree(gendev);
    }
}

/****************************************************************************/

/** Module initialization.
 *
 * Initializes \a master_count masters.
 * \return 0 on success, else < 0
 */
int __init ec_gen_init_module(void)
{
    int ret = 0;
    struct list_head descs;
    struct net_device *netdev;
    ec_gen_interface_desc_t *desc, *next;

    printk(KERN_INFO PFX "EtherCAT master generic Ethernet device module %s\n",
            EC_MASTER_VERSION);

    INIT_LIST_HEAD(&generic_devices);
    INIT_LIST_HEAD(&descs);

    ret = register_netdevice_notifier(&ec_gen_notifier);
    if (ret) {
        printk(KERN_ERR PFX "Failed to register netdevice notifier.\n");
        return ret;
    }

    rcu_read_lock();
    for_each_netdev_rcu(&init_net, netdev) {
        if (netdev->type != ARPHRD_ETHER)
            continue;
        desc = kmalloc(sizeof(ec_gen_interface_desc_t), GFP_ATOMIC);
        if (!desc) {
            ret = -ENOMEM;
            rcu_read_unlock();
            goto out_err;
        }
        strncpy(desc->name, netdev->name, IFNAMSIZ);
        desc->netdev = netdev;
        desc->ifindex = netdev->ifindex;
        memcpy(desc->dev_addr, netdev->dev_addr, ETH_ALEN);
        list_add_tail(&desc->list, &descs);
    }
    rcu_read_unlock();

    list_for_each_entry_safe(desc, next, &descs, list) {
        ret = offer_device(desc);
        if (ret) {
            goto out_err;
        }
        kfree(desc);
    }
    return ret;

out_err:
    list_for_each_entry_safe(desc, next, &descs, list) {
        list_del(&desc->list);
        kfree(desc);
    }
    clear_devices();
    unregister_netdevice_notifier(&ec_gen_notifier);
    return ret;
}

/****************************************************************************/

/** Module cleanup.
 *
 * Clears all master instances.
 */
void __exit ec_gen_cleanup_module(void)
{
    unregister_netdevice_notifier(&ec_gen_notifier);
    clear_devices();
    printk(KERN_INFO PFX "Unloading.\n");
}

/****************************************************************************/

/** \cond */

module_init(ec_gen_init_module);
module_exit(ec_gen_cleanup_module);

/** \endcond */

/****************************************************************************/
