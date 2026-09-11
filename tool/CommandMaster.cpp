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

#include "CommandMaster.h"

#include "MasterDevice.h"

#include <iostream>
#include <iomanip>

#define MAX_TIME_STR_SIZE 50

using std::string;
using std::stringstream;
using std::endl;
using std::cout;
using std::hex;
using std::dec;
using std::setw;
using std::setfill;
using std::setprecision;
using std::fixed;

/****************************************************************************/

CommandMaster::CommandMaster():
    Command("master", "Show master and Ethernet device information.")
{
}

/****************************************************************************/

string CommandMaster::helpString(const string &binaryBaseName) const
{
    stringstream str;

    str << binaryBaseName << " " << getName() << " [OPTIONS]" << endl
        << endl
        << getBriefDescription() << endl
        << endl
        << "Command-specific options:" << endl
        << "  --master -m <indices>  Master indices. A comma-separated"
        << endl
        << "                         list with ranges is supported." << endl
        << "                         Example: 1,4,5,7-9. Default: - (all)."
        << endl << endl
        << numericInfo();

    return str.str();
}

/****************************************************************************/

void CommandMaster::execute(const StringVector &args)
{
    MasterIndexList masterIndices;
    ec_ioctl_master_t data;
    stringstream err;

    if (args.size()) {
        err << "'" << getName() << "' takes no arguments!";
        throwInvalidUsageException(err);
    }

    masterIndices = getMasterIndices();

    if (getJson()) {
        cout << "[" << endl;
    }

    MasterIndexList::const_iterator mi;
    for (mi = masterIndices.begin();
            mi != masterIndices.end(); mi++) {
        MasterDevice m(*mi);
        m.open(MasterDevice::Read);
        m.getMaster(&data);

        if (getJson()) {
            if (mi != masterIndices.begin()) {
                cout << "," << endl;
            }
            showMasterJson(m, data);
        } else {
            showMaster(m, data);
        }
    }

    if (getJson()) {
        cout << endl << "]" << endl;
    }
}

/****************************************************************************/

void CommandMaster::showMaster(
        MasterDevice &m,
        const ec_ioctl_master_t &data
        )
{
    unsigned int dev_idx, j;
    time_t epoch;
    char time_str[MAX_TIME_STR_SIZE + 1];
    size_t time_str_size;

    {
        cout
            << "Master" << m.getIndex() << endl
            << "  Phase: ";

        switch (data.phase) {
            case 0:  cout << "Waiting for device(s)..."; break;
            case 1:  cout << "Idle"; break;
            case 2:  cout << "Operation"; break;
            default: cout << "???";
        }

        cout << endl
            << "  Active: " << (data.active ? "yes" : "no") << endl
            << "  Slaves: " << data.slave_count << endl
            << "  SII caching: ";

        bool first{true};
        if (data.sii_caching & EC_SII_VENDOR) {
            cout << "Vendor";
            first = false;
        }
        if (data.sii_caching & EC_SII_PRODUCT) {
            if (not first) {
                cout << " | ";
            }
            first = false;
            cout << "Product";
        }
        if (data.sii_caching & EC_SII_REVISION) {
            if (not first) {
                cout << " | ";
            }
            first = false;
            cout << "Revision";
        }
        if (data.sii_caching & EC_SII_SERIAL) {
            if (not first) {
                cout << " | ";
            }
            first = false;
            cout << "Serial";
        }
        if (data.sii_caching & EC_SII_ALIAS) {
            if (not first) {
                cout << " | ";
            }
            first = false;
            cout << "Alias";
        }
        if (first) {
            cout << "disabled";
        }

        cout << endl << "  Ethernet devices:" << endl;

        for (dev_idx = EC_DEVICE_MAIN; dev_idx < data.num_devices;
                dev_idx++) {
            cout << "    " << (dev_idx == EC_DEVICE_MAIN ? "Main" : "Backup")
                << ": ";
            cout << hex << setfill('0')
                << setw(2) << (unsigned int) data.devices[dev_idx].address[0]
                << ":"
                << setw(2) << (unsigned int) data.devices[dev_idx].address[1]
                << ":"
                << setw(2) << (unsigned int) data.devices[dev_idx].address[2]
                << ":"
                << setw(2) << (unsigned int) data.devices[dev_idx].address[3]
                << ":"
                << setw(2) << (unsigned int) data.devices[dev_idx].address[4]
                << ":"
                << setw(2) << (unsigned int) data.devices[dev_idx].address[5]
                << " ("
                << (data.devices[dev_idx].attached ?
                        "attached" : "waiting...")
                << ")" << endl << dec
                << "      Link: "
                << (data.devices[dev_idx].link_state ? "UP" : "DOWN") << endl
                << "      Tx frames:   "
                << data.devices[dev_idx].tx_count << endl
                << "      Tx bytes:    "
                << data.devices[dev_idx].tx_bytes << endl
                << "      Rx frames:   "
                << data.devices[dev_idx].rx_count << endl
                << "      Rx bytes:    "
                << data.devices[dev_idx].rx_bytes << endl
                << "      Tx errors:   "
                << data.devices[dev_idx].tx_errors << endl
                << "      Tx frame rate [1/s]: "
                << setfill(' ') << setprecision(0) << fixed;
            for (j = 0; j < EC_RATE_COUNT; j++) {
                cout << setw(ColWidth)
                    << data.devices[dev_idx].tx_frame_rates[j] / 1000.0;
                if (j < EC_RATE_COUNT - 1) {
                    cout << " ";
                }
            }
            cout << endl
                << "      Tx rate [KByte/s]:   "
                << setprecision(1) << fixed;
            for (j = 0; j < EC_RATE_COUNT; j++) {
                cout << setw(ColWidth)
                    << data.devices[dev_idx].tx_byte_rates[j] / 1024.0;
                if (j < EC_RATE_COUNT - 1) {
                    cout << " ";
                }
            }
            cout << endl
                << "      Rx frame rate [1/s]: "
                << setfill(' ') << setprecision(0) << fixed;
            for (j = 0; j < EC_RATE_COUNT; j++) {
                cout << setw(ColWidth)
                    << data.devices[dev_idx].rx_frame_rates[j] / 1000.0;
                if (j < EC_RATE_COUNT - 1) {
                    cout << " ";
                }
            }
            cout << endl
                << "      Rx rate [KByte/s]:   "
                << setprecision(1) << fixed;
            for (j = 0; j < EC_RATE_COUNT; j++) {
                cout << setw(ColWidth)
                    << data.devices[dev_idx].rx_byte_rates[j] / 1024.0;
                if (j < EC_RATE_COUNT - 1) {
                    cout << " ";
                }
            }
            cout << setprecision(0) << endl;
        }
        unsigned int lost = data.tx_count - data.rx_count;
        if (lost == 1) {
            // allow one frame travelling
            lost = 0;
        }
        cout << "    Common:" << endl
            << "      Tx frames:   "
            << data.tx_count << endl
            << "      Tx bytes:    "
            << data.tx_bytes << endl
            << "      Rx frames:   "
            << data.rx_count << endl
            << "      Rx bytes:    "
            << data.rx_bytes << endl
            << "      Lost frames: " << lost << endl
            << "      Tx frame rate [1/s]: "
            << setfill(' ') << setprecision(0) << fixed;
        for (j = 0; j < EC_RATE_COUNT; j++) {
            cout << setw(ColWidth)
                << data.tx_frame_rates[j] / 1000.0;
            if (j < EC_RATE_COUNT - 1) {
                cout << " ";
            }
        }
        cout << endl
            << "      Tx rate [KByte/s]:   "
            << setprecision(1) << fixed;
        for (j = 0; j < EC_RATE_COUNT; j++) {
            cout << setw(ColWidth)
                << data.tx_byte_rates[j] / 1024.0;
            if (j < EC_RATE_COUNT - 1) {
                cout << " ";
            }
        }
        cout << endl
            << "      Rx frame rate [1/s]: "
            << setfill(' ') << setprecision(0) << fixed;
        for (j = 0; j < EC_RATE_COUNT; j++) {
            cout << setw(ColWidth)
                << data.rx_frame_rates[j] / 1000.0;
            if (j < EC_RATE_COUNT - 1) {
                cout << " ";
            }
        }
        cout << endl
            << "      Rx rate [KByte/s]:   "
            << setprecision(1) << fixed;
        for (j = 0; j < EC_RATE_COUNT; j++) {
            cout << setw(ColWidth)
                << data.rx_byte_rates[j] / 1024.0;
            if (j < EC_RATE_COUNT - 1) {
                cout << " ";
            }
        }
        cout << endl
            << "      Loss rate [1/s]:     "
            << setprecision(0) << fixed;
        for (j = 0; j < EC_RATE_COUNT; j++) {
            cout << setw(ColWidth)
                << data.loss_rates[j] / 1000.0;
            if (j < EC_RATE_COUNT - 1) {
                cout << " ";
            }
        }
        cout << endl
            << "      Frame loss [%]:      "
            << setprecision(1) << fixed;
        for (j = 0; j < EC_RATE_COUNT; j++) {
            double perc = 0.0;
            if (data.tx_frame_rates[j]) {
                perc = 100.0 * data.loss_rates[j] / data.tx_frame_rates[j];
            }
            cout << setw(ColWidth) << perc;
            if (j < EC_RATE_COUNT - 1) {
                cout << " ";
            }
        }
        cout << setprecision(0) << endl;

        cout << "  Distributed clocks:" << endl
            << "    Reference clock:   ";
        if (data.ref_clock != 0xffff) {
            cout << "Slave " << dec << data.ref_clock;
        } else {
            cout << "None";
        }
        cout << endl
            << "    DC reference time: " << data.dc_ref_time << endl
            << "    Application time:  " << data.app_time << endl
            << "                       ";

        epoch = data.app_time / 1000000000 + 946684800ULL;
        time_str_size = strftime(time_str, MAX_TIME_STR_SIZE,
                "%Y-%m-%d %H:%M:%S", gmtime(&epoch));
        cout << string(time_str, time_str_size) << "."
            << setfill('0') << setw(9) << data.app_time % 1000000000 << endl;
    }
}

/****************************************************************************/

namespace {
    /** Prints one rate value keyed by its averaging interval.
     */
    void jsonRates(const char *key, const int32_t rates[], double divisor)
    {
        static const char *intervals[EC_RATE_COUNT] = {"1s", "10s", "60s"};
        unsigned int j;

        cout << "\"" << key << "\": {";
        for (j = 0; j < EC_RATE_COUNT; j++) {
            cout << "\"" << intervals[j] << "\": "
                << setprecision(3) << fixed << rates[j] / divisor;
            if (j < EC_RATE_COUNT - 1) {
                cout << ", ";
            }
        }
        cout << "}";
    }
}  // namespace

/****************************************************************************/

void CommandMaster::showMasterJson(
        MasterDevice &m,
        const ec_ioctl_master_t &data
        )
{
    unsigned int dev_idx, j;
    unsigned int lost;

    cout << "  {" << endl
        << "    \"master\": " << dec << m.getIndex() << "," << endl
        << "    \"phase\": \"";
    switch (data.phase) {
        case 0:  cout << "waiting"; break;
        case 1:  cout << "idle"; break;
        case 2:  cout << "operation"; break;
        default: cout << "unknown";
    }
    cout << "\"," << endl
        << "    \"active\": " << (data.active ? "true" : "false") << ","
        << endl
        << "    \"slave_count\": " << data.slave_count << "," << endl
        << "    \"sii_caching\": [";

    {
        bool first = true;
        static const struct { uint32_t flag; const char *name; } flags[] = {
            {EC_SII_VENDOR,   "vendor"},
            {EC_SII_PRODUCT,  "product"},
            {EC_SII_REVISION, "revision"},
            {EC_SII_SERIAL,   "serial"},
            {EC_SII_ALIAS,    "alias"},
        };
        for (j = 0; j < sizeof(flags) / sizeof(flags[0]); j++) {
            if (data.sii_caching & flags[j].flag) {
                if (!first) {
                    cout << ", ";
                }
                cout << "\"" << flags[j].name << "\"";
                first = false;
            }
        }
    }
    cout << "]," << endl
        << "    \"devices\": [" << endl;

    for (dev_idx = EC_DEVICE_MAIN; dev_idx < data.num_devices; dev_idx++) {
        cout << "      {" << endl
            << "        \"role\": \""
            << (dev_idx == EC_DEVICE_MAIN ? "main" : "backup") << "\","
            << endl
            << "        \"address\": \""
            << hex << setfill('0')
            << setw(2) << (unsigned int) data.devices[dev_idx].address[0]
            << ":"
            << setw(2) << (unsigned int) data.devices[dev_idx].address[1]
            << ":"
            << setw(2) << (unsigned int) data.devices[dev_idx].address[2]
            << ":"
            << setw(2) << (unsigned int) data.devices[dev_idx].address[3]
            << ":"
            << setw(2) << (unsigned int) data.devices[dev_idx].address[4]
            << ":"
            << setw(2) << (unsigned int) data.devices[dev_idx].address[5]
            << dec << setfill(' ') << "\"," << endl
            << "        \"attached\": "
            << (data.devices[dev_idx].attached ? "true" : "false")
            << "," << endl
            << "        \"link\": "
            << (data.devices[dev_idx].link_state ? "true" : "false")
            << "," << endl
            << "        \"tx_frames\": " << data.devices[dev_idx].tx_count
            << "," << endl
            << "        \"tx_bytes\": " << data.devices[dev_idx].tx_bytes
            << "," << endl
            << "        \"rx_frames\": " << data.devices[dev_idx].rx_count
            << "," << endl
            << "        \"rx_bytes\": " << data.devices[dev_idx].rx_bytes
            << "," << endl
            << "        \"tx_errors\": " << data.devices[dev_idx].tx_errors
            << "," << endl
            << "        ";
        jsonRates("tx_frame_rate", data.devices[dev_idx].tx_frame_rates,
                1000.0);
        cout << "," << endl << "        ";
        jsonRates("tx_rate", data.devices[dev_idx].tx_byte_rates, 1024.0);
        cout << "," << endl << "        ";
        jsonRates("rx_frame_rate", data.devices[dev_idx].rx_frame_rates,
                1000.0);
        cout << "," << endl << "        ";
        jsonRates("rx_rate", data.devices[dev_idx].rx_byte_rates, 1024.0);
        cout << endl
            << "      }";
        if (dev_idx + 1 < data.num_devices) {
            cout << ",";
        }
        cout << endl;
    }

    lost = data.tx_count - data.rx_count;
    if (lost == 1) {
        // allow one frame travelling
        lost = 0;
    }

    cout << "    ]," << endl
        << "    \"common\": {" << endl
        << "      \"tx_frames\": " << data.tx_count << "," << endl
        << "      \"tx_bytes\": " << data.tx_bytes << "," << endl
        << "      \"rx_frames\": " << data.rx_count << "," << endl
        << "      \"rx_bytes\": " << data.rx_bytes << "," << endl
        << "      \"lost_frames\": " << lost << "," << endl
        << "      ";
    jsonRates("tx_frame_rate", data.tx_frame_rates, 1000.0);
    cout << "," << endl << "      ";
    jsonRates("tx_rate", data.tx_byte_rates, 1024.0);
    cout << "," << endl << "      ";
    jsonRates("rx_frame_rate", data.rx_frame_rates, 1000.0);
    cout << "," << endl << "      ";
    jsonRates("rx_rate", data.rx_byte_rates, 1024.0);
    cout << "," << endl << "      ";
    jsonRates("loss_rate", data.loss_rates, 1000.0);
    cout << "," << endl
        << "      \"frame_loss_percent\": {";
    {
        static const char *intervals[EC_RATE_COUNT] = {"1s", "10s", "60s"};
        for (j = 0; j < EC_RATE_COUNT; j++) {
            double perc = 0.0;
            if (data.tx_frame_rates[j]) {
                perc = 100.0 * data.loss_rates[j] / data.tx_frame_rates[j];
            }
            cout << "\"" << intervals[j] << "\": "
                << setprecision(3) << fixed << perc;
            if (j < EC_RATE_COUNT - 1) {
                cout << ", ";
            }
        }
    }
    cout << "}" << endl
        << "    }," << endl
        << "    \"distributed_clocks\": {" << endl
        << "      \"reference_clock\": ";
    if (data.ref_clock != 0xffff) {
        cout << dec << data.ref_clock;
    } else {
        cout << "null";
    }
    cout << "," << endl
        << "      \"dc_ref_time_ns\": \"" << data.dc_ref_time << "\"," << endl
        << "      \"app_time_ns\": \"" << data.app_time << "\"," << endl
        << "      \"app_time\": \"";

    {
        time_t epoch;
        char time_str[MAX_TIME_STR_SIZE + 1];
        size_t time_str_size;

        epoch = data.app_time / 1000000000 + 946684800ULL;
        time_str_size = strftime(time_str, MAX_TIME_STR_SIZE,
                "%Y-%m-%dT%H:%M:%S", gmtime(&epoch));
        cout << string(time_str, time_str_size) << "."
            << setfill('0') << setw(9) << data.app_time % 1000000000
            << setfill(' ') << "Z";
    }

    cout << "\"" << endl
        << "    }" << endl
        << "  }";
}

/****************************************************************************/
