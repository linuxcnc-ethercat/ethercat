/*****************************************************************************
 *
 *  Copyright (C) 2024  Bjarne von Horn, Ingenieurgemeinschaft IgH
 *                2025  Florian Pose <fp@igh.de>
 *
 *  This file is part of the IgH EtherCAT master userspace library.
 *
 *  The IgH EtherCAT master userspace library is free software; you can
 *  redistribute it and/or modify it under the terms of the GNU Lesser General
 *  Public License as published by the Free Software Foundation; version 2.1
 *  of the License.
 *
 *  The IgH EtherCAT master userspace library is distributed in the hope that
 *  it will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with the IgH EtherCAT master userspace library. If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/

#include "fakeethercat.h"

#include <cstring>
#include <fstream>
#include <ios>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

#include <sys/stat.h>

/****************************************************************************/

static std::ostream &operator<<(std::ostream &os, const sdo_address &a)
{
    os << std::setfill('0') << std::hex << std::setw(6) << a.getCombined();
    return os;
}

static std::ostream &operator<<(std::ostream &os, const ec_address &a)
{
    os << std::setfill('0') << std::hex << std::setw(8) << a.getCombined();
    return os;
}

static void add_spaces(std::ostream &out, int const num)
{
    for (int i = 0; i < num; ++i) {
        out << ' ';
    }
}

template <typename Map, typename Func> static void
map2Json(std::ostream &out, const Map &map, Func &&print_func, int indent = 0)
{
    indent += 4;
    out << "{";
    bool is_first = true;
    for (const auto &kv : map) {
        if (is_first) {
            out << '\n';
            is_first = false;
        }
        else {
            out << ",\n";
        }
        add_spaces(out, indent);
        out << "\"0x" << std::hex << std::setfill('0')
            << std::setw(2 * sizeof(typename Map::key_type));
        out << kv.first << "\": ";
        print_func(out, kv.second);
    }
    out << '\n';
    add_spaces(out, indent - 4);
    out << "}";
}

/*****************************************************************************
 * Pdo
 ****************************************************************************/

size_t pdo::sizeInBytes() const
{
    size_t ans = 0;
    for (const auto &entry : entries) {
        ans += entry.bit_length;
    }
    return (ans + 7) / 8;
}

Offset pdo::findEntry(uint16_t idx, uint8_t subindex) const
{
    size_t offset_bits = 0;
    for (const auto &entry : entries) {
        if (entry.index == idx && entry.subindex == subindex) {
            return Offset(offset_bits / 8, offset_bits % 8);
        }
        offset_bits += entry.bit_length;
    }
    return NotFound;
}

Offset pdo::findEntryByPos(unsigned int entry_pos) const
{
    size_t offset_bits = 0;
    unsigned int pos {0};
    for (const auto &entry : entries) {
        if (pos == entry_pos) {
            return Offset(offset_bits / 8, offset_bits % 8);
        }
        offset_bits += entry.bit_length;
        pos++;
    }
    return NotFound;
}

/*****************************************************************************
 * Common
 ****************************************************************************/

unsigned int ecrt_version_magic(void)
{
    return ECRT_VERSION_MAGIC;
}

ec_master_t *ecrt_request_master(
        unsigned int master_index /**< Index of the master to request. */
)
{
    return new ec_master(master_index);
}

ec_master_t *ecrt_open_master(
        unsigned int master_index /**< Index of the master to request. */
)
{
    return new ec_master(master_index);
}

void ecrt_release_master(ec_master_t *master)
{
    delete master;
}

/*****************************************************************************
 * Master
 ****************************************************************************/

int ecrt_master_reserve(ec_master_t *master /**< EtherCAT master */
)
{
    return 0;
}

static const char *getPrefix()
{
    if (const auto ans = getenv("FAKE_EC_PREFIX")) {
        return ans;
    }
    return "/FakeEtherCAT";
}

ec_domain *ec_master::createDomain()
{
    domains.emplace_back(rt_ipc.get(), getPrefix(), this);
    return &domains.back();
}

ec_domain_t *
ecrt_master_create_domain(ec_master_t *master /**< EtherCAT master. */
)
{
    return master->createDomain();
}

ec_slave_config_t *ec_master::slave_config(
        uint16_t alias,       /**< Slave alias. */
        uint16_t position,    /**< Slave position. */
        uint32_t vendor_id,   /**< Expected vendor ID. */
        uint32_t product_code /**< Expected product code. */
)
{
    const ec_address address {alias, position};
    const auto it = slaves.find(address);
    if (it != slaves.end()) {
        if (it->second.vendor_id == vendor_id
            && it->second.product_code == product_code) {
            return &it->second;
        }
        else {
            std::cerr << "Attempted to reconfigure slave (" << alias << ","
                      << position << ")!\n";
            return nullptr;
        }
    }
    else {
        return &slaves.insert(std::make_pair(
                                      ec_address {address},
                                      ec_slave_config {
                                              address,
                                              vendor_id,
                                              product_code}))
                        .first->second;
    }
}

ec_slave_config_t *ecrt_master_slave_config(
        ec_master_t *master,  /**< EtherCAT master */
        uint16_t alias,       /**< Slave alias. */
        uint16_t position,    /**< Slave position. */
        uint32_t vendor_id,   /**< Expected vendor ID. */
        uint32_t product_code /**< Expected product code. */
)
{
    return master->slave_config(alias, position, vendor_id, product_code);
}

int ecrt_master_select_reference_clock(
        ec_master_t *master,  /**< EtherCAT master. */
        ec_slave_config_t *sc /**< Slave config of the slave to use as the
                               * reference slave (or NULL). */
)
{
    return 0;
}

int ecrt_master(
        ec_master_t *master,          /**< EtherCAT master */
        ec_master_info_t *master_info /**< Structure that will output
                                        the information */
)
{
    master_info->slave_count = 0;
    master_info->link_up = 1;
    master_info->scan_busy = 0;
    master_info->app_time = 0;
    return 0;
}

int ecrt_master_scan_progress(
        ec_master_t *master,                /**< EtherCAT master */
        ec_master_scan_progress_t *progress /**< Structure that will output
                                              the progress information. */
)
{
    progress->scan_index = progress->slave_count = master->getNoSlaves();
    return 0;
}

int ecrt_master_get_slave(
        ec_master_t *master,        /**< EtherCAT master */
        uint16_t slave_position,    /**< Slave position. */
        ec_slave_info_t *slave_info /**< Structure that will output the
                                      information */
)
{
    slave_info->position = slave_position;
    slave_info->vendor_id = 0x00000000;
    slave_info->product_code = 0x00000000;
    slave_info->revision_number = 0x00000000;
    slave_info->serial_number = 0x00000000;
    slave_info->alias = 0x0000;
    slave_info->current_on_ebus = 0;
    for (unsigned int i = 0; i < EC_MAX_PORTS; i++) {
        slave_info->ports[i].desc = EC_PORT_NOT_IMPLEMENTED;
        slave_info->ports[i].link.link_up = 0;
        slave_info->ports[i].link.loop_closed = 1;
        slave_info->ports[i].link.signal_detected = 0;
        slave_info->ports[i].next_slave = 0;
        slave_info->ports[i].delay_to_next_dc = 0;
    }
    slave_info->al_state = EC_AL_STATE_OP;
    slave_info->error_flag = 0;
    slave_info->sync_count = 8;
    slave_info->sdo_count = 0;
    slave_info->name[0] = 0;
    return 0;
}

int ecrt_master_get_sync_manager(
        ec_master_t *master,     /**< EtherCAT master. */
        uint16_t slave_position, /**< Slave position. */
        uint8_t sync_index,      /**< Sync manager index. Must be less
                                     than #EC_MAX_SYNC_MANAGERS. */
        ec_sync_info_t *sync     /**< Pointer to output structure. */
)
{
    sync->index = sync_index;
    sync->dir = EC_DIR_INVALID;
    sync->n_pdos = 0;
    sync->pdos = nullptr;
    sync->watchdog_mode = EC_WD_DEFAULT;
    return 0;
}

int ecrt_master_get_pdo(
        ec_master_t *master,     /**< EtherCAT master. */
        uint16_t slave_position, /**< Slave position. */
        uint8_t sync_index,      /**< Sync manager index. Must be less
                                      than #EC_MAX_SYNC_MANAGERS. */
        uint16_t pos,            /**< Zero-based PDO position. */
        ec_pdo_info_t *pdo       /**< Pointer to output structure. */
)
{
    return -ENOENT;
}

int ecrt_master_get_pdo_entry(
        ec_master_t *master,       /**< EtherCAT master. */
        uint16_t slave_position,   /**< Slave position. */
        uint8_t sync_index,        /**< Sync manager index. Must be less
                                        than #EC_MAX_SYNC_MANAGERS. */
        uint16_t pdo_pos,          /**< Zero-based PDO position. */
        uint16_t entry_pos,        /**< Zero-based PDO entry position. */
        ec_pdo_entry_info_t *entry /**< Pointer to output structure. */
)
{
    return -ENOENT;
}

int ecrt_master_sdo_download(
        ec_master_t *master,     /**< EtherCAT master. */
        uint16_t slave_position, /**< Slave position. */
        uint16_t index,          /**< Index of the SDO. */
        uint8_t subindex,        /**< Subindex of the SDO. */
        const uint8_t *data,     /**< Data buffer to download. */
        size_t data_size,        /**< Size of the data buffer. */
        uint32_t *abort_code     /**< Abort code of the SDO download. */
)
{
    return 0;
}

int ecrt_master_sdo_download_complete(
        ec_master_t *master,     /**< EtherCAT master. */
        uint16_t slave_position, /**< Slave position. */
        uint16_t index,          /**< Index of the SDO. */
        const uint8_t *data,     /**< Data buffer to download. */
        size_t data_size,        /**< Size of the data buffer. */
        uint32_t *abort_code     /**< Abort code of the SDO download. */
)
{
    return 0;
}

int ecrt_master_sdo_upload(
        ec_master_t *master,     /**< EtherCAT master. */
        uint16_t slave_position, /**< Slave position. */
        uint16_t index,          /**< Index of the SDO. */
        uint8_t subindex,        /**< Subindex of the SDO. */
        uint8_t *target,         /**< Target buffer for the upload. */
        size_t target_size,      /**< Size of the target buffer. */
        size_t *result_size,     /**< Uploaded data size. */
        uint32_t *abort_code     /**< Abort code of the SDO upload. */
)
{
    *result_size = 0;
    return 0;
}

int ecrt_master_write_idn(
        ec_master_t *master,     /**< EtherCAT master. */
        uint16_t slave_position, /**< Slave position. */
        uint8_t drive_no,        /**< Drive number. */
        uint16_t idn,        /**< SoE IDN (see ecrt_slave_config_idn()). */
        const uint8_t *data, /**< Pointer to data to write. */
        size_t data_size,    /**< Size of data to write. */
        uint16_t *error_code /**< Pointer to variable, where an SoE error code
                               can be stored. */
)
{
    return 0;
}

int ecrt_master_read_idn(
        ec_master_t *master,     /**< EtherCAT master. */
        uint16_t slave_position, /**< Slave position. */
        uint8_t drive_no,        /**< Drive number. */
        uint16_t idn,        /**< SoE IDN (see ecrt_slave_config_idn()). */
        uint8_t *target,     /**< Pointer to memory where the read data can be
                               stored. */
        size_t target_size,  /**< Size of the memory \a target points to. */
        size_t *result_size, /**< Actual size of the received data. */
        uint16_t *error_code /**< Pointer to variable, where an SoE error code
                               can be stored. */
)
{
    *result_size = 0;
    return 0;
}

int ec_master::activate()
{
    for (auto &domain : domains) {
        if (domain.activate()) {
            return -1;
        }
    }

    {
        std::ofstream out(rt_ipc_dir + "/" + rt_ipc_name + "_slaves.json");
        if (!out.is_open()) {
            std::cerr << "could not dump json.\n";
            return -1;
        }
        out << "{\n    \"slaves\": ";
        map2Json(
                out,
                slaves,
                [](std::ostream &out, const ec_slave_config &slave) {
                    slave.dumpJson(out, 8);
                },
                4);
        out << "\n}\n";
    }
    return rtipc_prepare(rt_ipc.get());
}

int ecrt_master_activate(ec_master_t *master /**< EtherCAT master. */
)
{
    try {
        return master->activate();
    }
    catch (const std::exception &e) {
        std::cerr << "Could not activate: " << e.what() << '\n';
        return -1;
    }
}

int ec_master::deactivate()
{
    rt_ipc.reset(rtipc_create(rt_ipc_name.c_str(), rt_ipc_dir.c_str()));

    domains.clear();
    slaves.clear();

    return 0;
}

int ecrt_master_deactivate(ec_master_t *master /**< EtherCAT master. */
)
{
    return master->deactivate();
}

int ecrt_master_set_send_interval(
        ec_master_t *master, /**< EtherCAT master. */
        size_t send_interval /**< Send interval in us */
)
{
    return 0;
}

int ecrt_master_send(ec_master_t *master /**< EtherCAT master. */
)
{
    return 0;
}

int ecrt_master_receive(ec_master_t *master /**< EtherCAT master. */
)
{
    return 0;
}

int ecrt_master_state(
        const ec_master_t *master, /**< EtherCAT master. */
        ec_master_state_t *state   /**< Structure to store the information. */
)
{
    state->slaves_responding = master->getNoSlaves();
    state->link_up = 1;
    state->al_states = 8;
    return 0;
}

int ecrt_master_link_state(
        const ec_master_t *master, /**< EtherCAT master. */
        unsigned int dev_idx,      /**< Index of the device (0 = main device,
                                     1 = first backup device, ...). */
        ec_master_link_state_t *state /**< Structure to store the information.
                                       */
)
{
    state->slaves_responding = master->getNoSlaves();
    state->al_states = 4;
    state->link_up = 1;
    return 0;
}

int ecrt_master_application_time(
        ec_master_t *master, /**< EtherCAT master. */
        uint64_t app_time    /**< Application time. */
)
{
    return 0;
}

int ecrt_master_sync_reference_clock(
        ec_master_t *master /**< EtherCAT master. */
)
{
    return 0;
}

int ecrt_master_sync_reference_clock_to(
        ec_master_t *master, /**< EtherCAT master. */
        uint64_t sync_time   /**< Sync reference clock to this time. */
)
{
    return 0;
}

int ecrt_master_sync_slave_clocks(ec_master_t *master /**< EtherCAT master. */
)
{
    return 0;
}

int ecrt_master_reference_clock_time(
        const ec_master_t *master, /**< EtherCAT master. */
        uint32_t *time /**< Pointer to store the queried system time. */
)
{
    *time = 0;
    return 0;
}

int ecrt_master_sync_monitor_queue(
        ec_master_t *master /**< EtherCAT master. */
)
{
    return 0;
}

uint32_t ecrt_master_sync_monitor_process(
        const ec_master_t *master /**< EtherCAT master. */
)
{
    return 32;
}

int ecrt_master_reset(ec_master_t *master /**< EtherCAT master. */
)
{
    return 0;
}

static int mkpath(const std::string &file_path)
{
    if (file_path.empty()) {
        return 0;
    }

    std::size_t offset = 0;
    do {
        offset = file_path.find('/', offset + 1);
        const auto subpath = file_path.substr(0, offset);
        if (mkdir(subpath.c_str(), 0755) == -1) {
            if (errno != EEXIST) {
                return -1;
            }
        }
    } while (offset != std::string::npos);
    return 0;
}

static std::string getRtIpcDir(int idx)
{
    std::string ans("/tmp/FakeEtherCAT");
    if (const auto e = getenv("FAKE_EC_HOMEDIR")) {
        ans = e;
    }
    ans += std::string("/") + std::to_string(idx);
    mkpath(ans);
    return ans;
}

static const char *getName()
{
    static const char *default_name = "FakeEtherCAT";

    if (const auto ans = getenv("FAKE_EC_NAME")) {
        return ans;
    }

    std::cerr << "\nThe environment variable \"FAKE_EC_NAME\" is not set.\n"
              << "Using the default value \"" << default_name << "\".\n"
              << "Please consider to set unique names when using multiple"
              << " instances.\n\n";

    return default_name;
}

ec_master::ec_master(int id):
    rt_ipc_dir(getRtIpcDir(id)),
    rt_ipc_name(getName()),
    rt_ipc(rtipc_create(rt_ipc_name.c_str(), rt_ipc_dir.c_str())),
    id_(id)
{}

/*****************************************************************************
 * Slave Configuration
 ****************************************************************************/

int ecrt_slave_config_sync_manager(
        ec_slave_config_t *sc,           /**< Slave configuration. */
        uint8_t sync_index,              /**< Sync manager index. Must be less
                                           than #EC_MAX_SYNC_MANAGERS. */
        ec_direction_t direction,        /**< Input/Output. */
        ec_watchdog_mode_t watchdog_mode /** Watchdog mode. */
)
{
    auto &syncManager = sc->sync_managers[sync_index];
    syncManager.dir = direction;

    return 0;
}

int ecrt_slave_config_watchdog(
        ec_slave_config_t *sc,      /**< Slave configuration. */
        uint16_t watchdog_divider,  /**< Number of 40 ns intervals (register
                                      0x0400). Used as a base unit for all
                                      slave watchdogs^. If set to zero, the
                                      value is not written, so the default is
                                      used. */
        uint16_t watchdog_intervals /**< Number of base intervals for sync
                                      manager watchdog (register 0x0420). If
                                      set to zero, the value is not written,
                                      so the default is used. */
)
{
    return 0;
}

int ecrt_slave_config_pdo_mode(
        ec_slave_config_t *sc,     /**< Slave configuration. */
        ec_pdo_mode_t assign_mode, /**< Mode for the PDO assignment. */
        ec_pdo_mode_t config_mode  /**< Mode for the PDO configuration. */
)
{
    return 0;
}

int ecrt_slave_config_pdo_assign_add(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint8_t sync_index,    /**< Sync manager index. Must be less
                                 than #EC_MAX_SYNC_MANAGERS. */
        uint16_t index         /**< Index of the PDO to assign. */
)
{
    auto &syncManager = sc->sync_managers[sync_index];
    syncManager.pdos[index];
    return 0;
}

int ecrt_slave_config_pdo_assign_clear(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint8_t sync_index     /**< Sync manager index. Must be less
                                  than #EC_MAX_SYNC_MANAGERS. */
)
{
    auto &syncManager = sc->sync_managers[sync_index];
    syncManager.pdos.clear();
    return 0;
}

int ecrt_slave_config_pdo_mapping_add(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint16_t pdo_index,    /**< Index of the PDO. */
        uint16_t entry_index,  /**< Index of the PDO entry to add to the PDO's
                                 mapping. */
        uint8_t entry_subindex,  /**< Subindex of the PDO entry to add to the
                                   PDO's mapping. */
        uint8_t entry_bit_length /**< Size of the PDO entry in bit. */
)
{
    for (auto smIt = sc->sync_managers.begin();
         smIt != sc->sync_managers.end();
         ++smIt) {
        auto pdo_it = smIt->second.pdos.find(pdo_index);
        if (pdo_it == smIt->second.pdos.end()) {
            continue;
        }

        ec_pdo_entry_info_t entry_info;
        entry_info.index = entry_index;
        entry_info.subindex = entry_subindex;
        entry_info.bit_length = entry_bit_length;
        pdo_it->second.entries.push_back(entry_info);
        return 0;
    }

    std::cerr << __func__ << "(): PDO " << std::hex << pdo_index
              << " not found." << std::endl;
    return -1;
}

int ecrt_slave_config_pdo_mapping_clear(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint16_t pdo_index     /**< Index of the PDO. */
)
{
    for (auto smIt = sc->sync_managers.begin();
         smIt != sc->sync_managers.end();
         ++smIt) {
        auto pdo_it = smIt->second.pdos.find(pdo_index);
        if (pdo_it == smIt->second.pdos.end()) {
            continue;
        }

        pdo_it->second.entries.clear();
        return 0;
    }

    std::cerr << __func__ << "(): PDO " << std::hex << pdo_index
              << " not found." << std::endl;
    return -1;
}

int ecrt_slave_config_pdos(
        ec_slave_config_t *sc, /**< Slave configuration. */
        unsigned int n_syncs,  /**< Number of sync manager configurations in
                                 \a syncs. */
        const ec_sync_info_t syncs[] /**< Array of sync manager
                                       configurations. */
)
{
    if (!syncs) {
        return 0;
    }
    for (unsigned int sync_idx = 0; sync_idx < n_syncs; ++sync_idx) {
        if (syncs[sync_idx].index == 0xff) {
            return 0;
        }
        auto &syncManager = sc->sync_managers[syncs[sync_idx].index];
        syncManager.dir = syncs[sync_idx].dir;
        for (unsigned int i = 0; i < syncs[sync_idx].n_pdos; ++i) {
            const auto &in_pdo = syncs[sync_idx].pdos[i];
            if (in_pdo.n_entries == 0 || !in_pdo.entries) {
                std::cerr << "Default mapping not supported.";
                return -1;
            }
            auto &out_pdo = syncManager.pdos[in_pdo.index];
            for (unsigned int pdo_entry_idx = 0;
                 pdo_entry_idx < in_pdo.n_entries;
                 ++pdo_entry_idx) {
                out_pdo.entries.push_back(in_pdo.entries[pdo_entry_idx]);
            }
        }
    }

    return 0;
}

int ecrt_slave_config_reg_pdo_entry(
        ec_slave_config_t *sc,  /**< Slave configuration. */
        uint16_t entry_index,   /**< Index of the PDO entry to register. */
        uint8_t entry_subindex, /**< Subindex of the PDO entry to register. */
        ec_domain_t *domain,    /**< Domain. */
        unsigned int *bit_position /**< Optional address if bit addressing
                                 is desired */
)
{
    for (auto sync_it : sc->sync_managers) {
        for (auto pdo_it : sync_it.second.pdos) {
            const auto offset =
                    pdo_it.second.findEntry(entry_index, entry_subindex);
            if (offset != NotFound) {
                const auto domain_offset =
                        domain->map(*sc, sync_it.first, pdo_it.first);
                if (domain_offset != -1) {
                    if (bit_position) {
                        *bit_position = offset.bits;
                    }
                    else if (offset.bits) {
                        std::cerr << "Pdo Entry is not byte aligned"
                                  << " but bit offset is ignored!\n";
                        return -1;
                    }
                    return domain_offset + offset.bytes;
                }
                else {
                    return -1;
                }
            }
        }
    }
    return -1;  // offset
}

int ecrt_slave_config_reg_pdo_entry_pos(
        ec_slave_config_t *sc,  /**< Slave configuration. */
        uint8_t sync_index,     /**< Sync manager index. */
        unsigned int pdo_pos,   /**< Position of the PDO inside the SM. */
        unsigned int entry_pos, /**< Position of the entry inside the PDO. */
        ec_domain_t *domain,    /**< Domain. */
        unsigned int *bit_position /**< Optional address if bit addressing
                                 is desired */
)
{
    auto syncIt {sc->sync_managers.find(sync_index)};
    if (syncIt == sc->sync_managers.end()) {
        return -1;
    }

    auto pdo_it {syncIt->second.pdos.find(pdo_pos)};
    if (pdo_it == syncIt->second.pdos.end()) {
        return -1;
    }

    const auto offset = pdo_it->second.findEntryByPos(entry_pos);
    if (offset != NotFound) {
        return -1;
    }

    const auto domain_offset = domain->map(*sc, sync_index, pdo_it->first);
    if (domain_offset == -1) {
        return -1;
    }

    if (bit_position) {
        *bit_position = offset.bits;
    }
    else if (offset.bits) {
        std::cerr << "Pdo Entry is not byte aligned"
                  << " but bit offset is ignored!\n";
        return -1;
    }
    return domain_offset + offset.bytes;
}

int ecrt_slave_config_dc(
        ec_slave_config_t *sc,    /**< Slave configuration. */
        uint16_t assign_activate, /**< AssignActivate word. */
        uint32_t sync0_cycle,     /**< SYNC0 cycle time [ns]. */
        int32_t sync0_shift,      /**< SYNC0 shift time [ns]. */
        uint32_t sync1_cycle,     /**< SYNC1 cycle time [ns]. */
        int32_t sync1_shift       /**< SYNC1 shift time [ns]. */
)
{
    return 0;
}

int ecrt_slave_config_sdo(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint16_t index,        /**< Index of the SDO to configure. */
        uint8_t subindex,      /**< Subindex of the SDO to configure. */
        const uint8_t *data,   /**< Pointer to the data. */
        size_t size            /**< Size of the \a data. */
)
{
    sc->sdos[sdo_address {index, subindex}] =
            std::basic_string<uint8_t>(data, data + size);
    return 0;
}

int ecrt_slave_config_sdo8(
        ec_slave_config_t *sc, /**< Slave configuration */
        uint16_t sdo_index,    /**< Index of the SDO to configure. */
        uint8_t sdo_subindex,  /**< Subindex of the SDO to configure. */
        uint8_t value          /**< Value to set. */
)
{
    return ecrt_slave_config_sdo(sc, sdo_index, sdo_subindex, &value, 1);
}

int ecrt_slave_config_sdo16(
        ec_slave_config_t *sc, /**< Slave configuration */
        uint16_t sdo_index,    /**< Index of the SDO to configure. */
        uint8_t sdo_subindex,  /**< Subindex of the SDO to configure. */
        uint16_t const value   /**< Value to set. */
)
{
    uint8_t buf[sizeof(value)];
    memcpy(&buf, &value, sizeof(value));
    return ecrt_slave_config_sdo(
            sc,
            sdo_index,
            sdo_subindex,
            buf,
            sizeof(buf));
}

int ecrt_slave_config_sdo32(
        ec_slave_config_t *sc, /**< Slave configuration */
        uint16_t sdo_index,    /**< Index of the SDO to configure. */
        uint8_t sdo_subindex,  /**< Subindex of the SDO to configure. */
        uint32_t const value   /**< Value to set. */
)
{
    uint8_t buf[sizeof(value)];
    memcpy(&buf, &value, sizeof(value));
    return ecrt_slave_config_sdo(
            sc,
            sdo_index,
            sdo_subindex,
            buf,
            sizeof(buf));
}

int ecrt_slave_config_complete_sdo(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint16_t index,        /**< Index of the SDO to configure. */
        const uint8_t *data,   /**< Pointer to the data. */
        size_t size            /**< Size of the \a data. */
)
{
    return ecrt_slave_config_sdo(sc, index, 0, data, size);
}

int ecrt_slave_config_emerg_size(
        ec_slave_config_t *sc, /**< Slave configuration. */
        size_t elements /**< Number of records of the CoE emergency ring. */
)
{
    return 0;
}

int ecrt_slave_config_emerg_pop(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint8_t *target        /**< Pointer to target memory (at least
                                 EC_COE_EMERGENCY_MSG_SIZE bytes). */
)
{
    return -ENOENT;
}

int ecrt_slave_config_emerg_clear(
        ec_slave_config_t *sc /**< Slave configuration. */
)
{
    return 0;
}

int ecrt_slave_config_emerg_overruns(
        const ec_slave_config_t *sc /**< Slave configuration. */
)
{
    return 0;
}

ec_sdo_request_t *ecrt_slave_config_create_sdo_request(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint16_t index,        /**< SDO index. */
        uint8_t subindex,      /**< SDO subindex. */
        size_t size            /**< Data size to reserve. */
)
{
    return nullptr;
}

ec_soe_request_t *ecrt_slave_config_create_soe_request(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint8_t drive_no,      /**< Drive number. */
        uint16_t idn,          /**< Sercos ID-Number. */
        size_t size            /**< Data size to reserve. */
)
{
    return nullptr;
}

ec_voe_handler_t *ecrt_slave_config_create_voe_handler(
        ec_slave_config_t *sc, /**< Slave configuration. */
        size_t size            /**< Data size to reserve. */
)
{
    return nullptr;
}

ec_reg_request_t *ecrt_slave_config_create_reg_request(
        ec_slave_config_t *sc, /**< Slave configuration. */
        size_t size            /**< Data size to reserve. */
)
{
    return nullptr;
}

int ecrt_slave_config_state(
        const ec_slave_config_t *sc,   /**< Slave configuration */
        ec_slave_config_state_t *state /**< State object to write to. */
)
{
    state->online = 1;
    state->operational = 1;
    state->al_state = EC_AL_STATE_OP;
    return 0;
}

int ecrt_slave_config_idn(
        ec_slave_config_t *sc, /**< Slave configuration. */
        uint8_t drive_no,      /**< Drive number. */
        uint16_t idn,          /**< SoE IDN. */
        ec_al_state_t state,   /**< AL state in which to write
                                 the IDN (PREOP or   SAFEOP). */
        const uint8_t *data,   /**< Pointer to the data. */
        size_t size            /**< Size of the \a data. */
)
{
    return 0;
}

int ecrt_slave_config_flag(
        ec_slave_config_t *sc, /**< Slave configuration. */
        const char *key,       /**< Key as null-terminated ASCII string. */
        int32_t value          /**< Value to store. */
)
{
    return 0;
}

int ecrt_slave_config_eoe_mac_address(
        ec_slave_config_t *sc,           /**< Slave configuration. */
        const unsigned char *mac_address /**< MAC address. */
)
{
    return 0;
}

int ecrt_slave_config_eoe_ip_address(
        ec_slave_config_t *sc,    /**< Slave configuration. */
        struct in_addr ip_address /**< IPv4 address. */
)
{
    return 0;
}

int ecrt_slave_config_eoe_subnet_mask(
        ec_slave_config_t *sc,     /**< Slave configuration. */
        struct in_addr subnet_mask /**< IPv4 subnet mask. */
)
{
    return 0;
}

int ecrt_slave_config_eoe_default_gateway(
        ec_slave_config_t *sc,         /**< Slave configuration. */
        struct in_addr gateway_address /**< Gateway's IPv4 address. */
)
{
    return 0;
}

int ecrt_slave_config_eoe_dns_address(
        ec_slave_config_t *sc,     /**< Slave configuration. */
        struct in_addr dns_address /**< IPv4 address of the DNS server. */
)
{
    return 0;
}

int ecrt_slave_config_eoe_hostname(
        ec_slave_config_t *sc, /**< Slave configuration. */
        const char *name       /**< Zero-terminated host name. */
)
{
    return 0;
}

int ecrt_slave_config_state_timeout(
        ec_slave_config_t *sc,    /**< Slave configuration. */
        ec_al_state_t from_state, /**< Initial state. */
        ec_al_state_t to_state,   /**< Target state. */
        unsigned int timeout_ms   /**< Timeout in [ms]. */
)
{
    return 0;
}

/*****************************************************************************
 * Domain
 ****************************************************************************/

int ecrt_domain_reg_pdo_entry_list(
        ec_domain_t *domain,                     /**< Domain. */
        const ec_pdo_entry_reg_t *pdo_entry_regs /**< Array of PDO
                                                   registrations. */
)
{
    const ec_pdo_entry_reg_t *reg;
    ec_slave_config_t *sc;
    int ret;

    for (reg = pdo_entry_regs; reg->index; reg++) {
        if (!(sc = ecrt_master_slave_config(
                      domain->getMaster(),
                      reg->alias,
                      reg->position,
                      reg->vendor_id,
                      reg->product_code))) {
            return -ENOENT;
        }

        if ((ret = ecrt_slave_config_reg_pdo_entry(
                     sc,
                     reg->index,
                     reg->subindex,
                     domain,
                     reg->bit_position))
            < 0) {
            return ret;
        }

        *reg->offset = ret;
    }

    return 0;
}

size_t ecrt_domain_size(const ec_domain_t *domain /**< Domain. */
)
{
    return domain->getSize();
}

uint8_t *ecrt_domain_data(const ec_domain_t *domain)
{
    return domain->getData();
}

int ecrt_domain_process(ec_domain_t *domain)
{
    return domain->process();
}

int ecrt_domain_queue(ec_domain_t *domain)
{
    return domain->queue();
}

int ecrt_domain_state(
        const ec_domain_t *domain, /**< Domain. */
        ec_domain_state_t *state   /**< Pointer to a state object to
                                     store the information. */
)
{
    state->working_counter = domain->getNumSlaves();
    state->redundancy_active = 0;
    state->wc_state = EC_WC_COMPLETE;
    return 0;
}

ec_domain::ec_domain(rtipc *rtipc, const char *prefix, ec_master_t *master):
    rt_group(rtipc_create_group(rtipc, 1.0)),
    prefix(prefix),
    master(master)
{}

int ec_domain::activate()
{
    std::unordered_set<uint32_t> slaves;

    connected.resize(mapped_pdos.size());
    size_t idx = 0;
    for (const auto &pdo : mapped_pdos) {
        slaves.insert(pdo.slave_address.getCombined());
        void *rt_pdo = nullptr;
        char buf[512];
        const auto fmt = snprintf(
                buf,
                sizeof(buf),
                "%s/%d/%08X/%04X",
                prefix,
                master->getId(),
                pdo.slave_address.getCombined(),
                pdo.pdo_index);
        if (fmt < 0 || fmt >= (int) sizeof(buf)) {
            return -ENOBUFS;
        }

        switch (pdo.dir) {
            case EC_DIR_OUTPUT:
                rt_pdo = rtipc_txpdo(
                        rt_group,
                        buf,
                        rtipc_uint8_T,
                        data.data() + pdo.offset,
                        pdo.size_bytes);
                std::cerr << "Registering " << buf << " as Output\n";
                break;
            case EC_DIR_INPUT:
                rt_pdo = rtipc_rxpdo(
                        rt_group,
                        buf,
                        rtipc_uint8_T,
                        data.data() + pdo.offset,
                        pdo.size_bytes,
                        connected.data() + idx);
                std::cerr << "Registering " << buf << " as Input\n";
                break;
            default:
                std::cerr << "Unknown direction " << pdo.dir << '\n';
                return -1;
        }
        if (!rt_pdo) {
            std::cerr << "Failed to register RtIPC PDO\n";
            return -1;
        }
        ++idx;
    }
    activated_ = true;
    numSlaves = slaves.size();
    return 0;
}

int ec_domain::process()
{
    rtipc_rx(rt_group);
    return 0;
}

int ec_domain::queue()
{
    rtipc_tx(rt_group);
    return 0;
}

ssize_t ec_domain::map(
        ec_slave_config const &config,
        unsigned int syncManager,
        uint16_t pdo_index)
{
    if (activated_) {
        return -1;
    }
    for (const auto &pdo : mapped_pdos) {
        if (pdo.slave_address == config.address
            && syncManager == pdo.syncManager && pdo_index == pdo.pdo_index) {
            // already mapped;
            return pdo.offset;
        }
    }
    const auto ans = data.size();
    const auto size = config.sync_managers.at(syncManager)
                              .pdos.at(pdo_index)
                              .sizeInBytes();
    mapped_pdos.emplace_back(
            ans,
            size,
            config.address,
            syncManager,
            pdo_index,
            config.sync_managers.at(syncManager).dir);
    data.resize(ans + size);
    return ans;
}

/*****************************************************************************
 * SDO requests
 ****************************************************************************/

int ecrt_sdo_request_index(
        ec_sdo_request_t *req, /**< SDO request. */
        uint16_t index,        /**< SDO index. */
        uint8_t subindex       /**< SDO subindex. */
)
{
    return 0;
}

int ecrt_sdo_request_timeout(
        ec_sdo_request_t *req, /**< SDO request. */
        uint32_t timeout       /**< Timeout in milliseconds. Zero
                                 means no timeout. */
)
{
    return 0;
}

uint8_t *
ecrt_sdo_request_data(const ec_sdo_request_t *req /**< SDO request. */
)
{
    return nullptr;
}

size_t
ecrt_sdo_request_data_size(const ec_sdo_request_t *req /**< SDO request. */
)
{
    return 0;
}

ec_request_state_t
ecrt_sdo_request_state(ec_sdo_request_t *req /**< SDO request. */
)
{
    return EC_REQUEST_ERROR;
}

int ecrt_sdo_request_write(ec_sdo_request_t *req /**< SDO request. */
)
{
    return 0;
}

int ecrt_sdo_request_read(ec_sdo_request_t *req /**< SDO request. */
)
{
    return 0;
}

/*****************************************************************************
 * SoE request
 ****************************************************************************/

int ecrt_soe_request_idn(
        ec_soe_request_t *req, /**< IDN request. */
        uint8_t drive_no,      /**< SDO index. */
        uint16_t idn           /**< SoE IDN. */
)
{
    return 0;
}

int ecrt_soe_request_timeout(
        ec_soe_request_t *req, /**< SoE request. */
        uint32_t timeout       /**< Timeout in milliseconds. Zero
                                 means no timeout. */
)
{
    return 0;
}

uint8_t *
ecrt_soe_request_data(const ec_soe_request_t *req /**< SoE request. */
)
{
    return nullptr;
}

size_t
ecrt_soe_request_data_size(const ec_soe_request_t *req /**< SoE request. */
)
{
    return 0;
}

ec_request_state_t
ecrt_soe_request_state(ec_soe_request_t *req /**< SoE request. */
)
{
    return EC_REQUEST_ERROR;
}

int ecrt_soe_request_write(ec_soe_request_t *req /**< SoE request. */
)
{
    return 0;
}

int ecrt_soe_request_read(ec_soe_request_t *req /**< SoE request. */
)
{
    return 0;
}

/*****************************************************************************
 * VoE handler
 ****************************************************************************/

int ecrt_voe_handler_send_header(
        ec_voe_handler_t *voe, /**< VoE handler. */
        uint32_t vendor_id,    /**< Vendor ID. */
        uint16_t vendor_type   /**< Vendor-specific type. */
)
{
    return 0;
}

int ecrt_voe_handler_received_header(
        const ec_voe_handler_t *voe, /**< VoE handler. */
        uint32_t *vendor_id,         /**< Vendor ID. */
        uint16_t *vendor_type        /**< Vendor-specific type. */
)
{
    *vendor_id = 0;
    *vendor_type = 0;
    return 0;
}

uint8_t *
ecrt_voe_handler_data(const ec_voe_handler_t *voe /**< VoE handler. */
)
{
    return nullptr;
}

size_t
ecrt_voe_handler_data_size(const ec_voe_handler_t *voe /**< VoE handler. */
)
{
    return 0;
}

int ecrt_voe_handler_write(
        ec_voe_handler_t *voe, /**< VoE handler. */
        size_t size /**< Number of bytes to write (without the VoE header). */
)
{
    return 0;
}

int ecrt_voe_handler_read(ec_voe_handler_t *voe /**< VoE handler. */
)
{
    return 0;
}

int ecrt_voe_handler_read_nosync(ec_voe_handler_t *voe /**< VoE handler. */
)
{
    return 0;
}

ec_request_state_t
ecrt_voe_handler_execute(ec_voe_handler_t *voe /**< VoE handler. */
)
{
    return EC_REQUEST_ERROR;
}

/*****************************************************************************
 * Register request
 ****************************************************************************/

uint8_t *
ecrt_reg_request_data(const ec_reg_request_t *req /**< Register request. */
)
{
    return nullptr;
}

ec_request_state_t
ecrt_reg_request_state(const ec_reg_request_t *req /**< Register request. */
)
{
    return EC_REQUEST_ERROR;
}

int ecrt_reg_request_write(
        ec_reg_request_t *req, /**< Register request. */
        uint16_t address,      /**< Register address. */
        size_t size            /**< Size to write. */
)
{
    return 0;
}

int ecrt_reg_request_read(
        ec_reg_request_t *req, /**< Register request. */
        uint16_t address,      /**< Register address. */
        size_t size            /**< Size to write. */
)
{
    return 0;
}

/*****************************************************************************
 * Floating-point read functions
 ****************************************************************************/

void ecrt_write_lreal(void *data, double const value)
{
    memcpy(data, &value, sizeof(value));
}

void ecrt_write_real(void *data, float const value)
{
    memcpy(data, &value, sizeof(value));
}

float ecrt_read_real(const void *data)
{
    float ans;
    memcpy(&ans, data, sizeof(ans));
    return ans;
}

double ecrt_read_lreal(const void *data)
{
    double ans;
    memcpy(&ans, data, sizeof(ans));
    return ans;
}

/****************************************************************************/

void ec_slave_config::dumpJson(std::ostream &out, int indent) const
{
    out << "{\n";
    indent += 4;
    add_spaces(out, indent);
    out << "\"vendor_id\": " << std::dec << vendor_id << ",\n";
    add_spaces(out, indent);
    out << "\"product_id\": " << product_code << ",\n";
    add_spaces(out, indent);
    out << "\"sdos\": ";
    map2Json(
            out,
            sdos,
            [](std::ostream &out, const std::basic_string<uint8_t> &value) {
                out << "\"0x";
                for (const auto s : value) {
                    out << std::hex << std::setfill('0') << std::setw(2)
                        << (unsigned) s;
                }
                out << '"';
            },
            indent);
    out << '\n';
    add_spaces(out, indent - 4);
    out << "}";
}

/****************************************************************************/
