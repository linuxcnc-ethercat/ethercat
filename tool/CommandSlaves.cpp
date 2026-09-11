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

#include "CommandSlaves.h"

#include "MasterDevice.h"

#include <string.h>

#include <iostream>
#include <iomanip>
#include <list>

using std::string;
using std::stringstream;
using std::endl;
using std::cout;
using std::list;
using std::hex;
using std::dec;
using std::setfill;
using std::setw;
using std::left;
using std::right;

/****************************************************************************/

CommandSlaves::CommandSlaves():
    Command("slaves", "Display slaves on the bus.")
{
}

/****************************************************************************/

string CommandSlaves::helpString(const string &binaryBaseName) const
{
    stringstream str;

    str << binaryBaseName << " " << getName() << " [OPTIONS]" << endl
        << endl
        << getBriefDescription() << endl
        << endl
        << "If the --verbose option is not given, the slaves are" << endl
        << "displayed one-per-line. Example:" << endl
        << endl
        << "1  5555:0  PREOP  +  EL3162 2C. Ana. Input 0-10V" << endl
        << "|  |    |  |      |  |" << endl
        << "|  |    |  |      |  \\- Name from the SII if available," << endl
        << "|  |    |  |      |     otherwise vendor ID and product" << endl
        << "|  |    |  |      |     code (both hexadecimal)." << endl
        << "|  |    |  |      \\- Error flag. '+' means no error," << endl
        << "|  |    |  |         'E' means that scan or" << endl
        << "|  |    |  |         configuration failed." << endl
        << "|  |    |  \\- Current application-layer state." << endl
        << "|  |    \\- Decimal relative position to the last" << endl
        << "|  |       slave with an alias address set." << endl
        << "|  \\- Decimal alias address of this slave (if set)," << endl
        << "|     otherwise of the last slave with an alias set," << endl
        << "|     or zero, if no alias was encountered up to this" << endl
        << "|     position." << endl
        << "\\- Absolute ring position in the bus." << endl
        << endl
        << "If the --verbose option is given, a detailed (multi-line)" << endl
        << "description is output for each slave." << endl
        << endl
        << "Slave selection:" << endl
        << "  Slaves for this and other commands can be selected with" << endl
        << "  the --alias and --position parameters as follows:" << endl
        << endl
        << "  1) If neither the --alias nor the --position option" << endl
        << "     is given, all slaves are selected." << endl
        << "  2) If only the --position option is given, it is" << endl
        << "     interpreted as an absolute ring position and" << endl
        << "     a slave with this position is matched." << endl
        << "  3) If only the --alias option is given, all slaves" << endl
        << "     with the given alias address and subsequent" << endl
        << "     slaves before a slave with a different alias" << endl
        << "     address match (use -p0 if only the slaves" << endl
        << "     with the given alias are desired, see 4))." << endl
        << "  4) If both the --alias and the --position option are" << endl
        << "     given, the latter is interpreted as relative" << endl
        << "     position behind any slave with the given alias." << endl
        << endl
        << "Command-specific options:" << endl
        << "  --alias    -a <alias>  Slave alias (see above)." << endl
        << "  --position -p <pos>    Slave position (see above)." << endl
        << "  --verbose  -v          Show detailed slave information." << endl
        << endl
        << numericInfo();

    return str.str();
}

/****************************************************************************/

void CommandSlaves::execute(const StringVector &args)
{
    MasterIndexList masterIndices;
    SlaveList slaves;
    bool doIndent;

    if (args.size()) {
        stringstream err;
        err << "'" << getName() << "' takes no arguments!";
        throwInvalidUsageException(err);
    }

    masterIndices = getMasterIndices();
    doIndent = masterIndices.size() > 1;

    if (getJson()) {
        cout << "[" << endl;
    }

    MasterIndexList::const_iterator mi;
    for (mi = masterIndices.begin();
            mi != masterIndices.end(); mi++) {
        MasterDevice m(*mi);
        m.open(MasterDevice::Read);
        slaves = selectedSlaves(m);

        if (getJson()) {
            if (mi != masterIndices.begin()) {
                cout << "," << endl;
            }
            showSlavesJson(m, slaves);
        } else if (getVerbosity() == Verbose) {
            showSlaves(m, slaves);
        } else {
            listSlaves(m, slaves, doIndent);
        }
    }

    if (getJson()) {
        cout << endl << "]" << endl;
    }
}

/****************************************************************************/

void CommandSlaves::listSlaves(
        MasterDevice &m,
        const SlaveList &slaves,
        bool doIndent
        )
{
    ec_ioctl_master_t master;
    unsigned int i, lastDevice;
    ec_ioctl_slave_t slave;
    uint16_t lastAlias, aliasIndex;
    Info info;
    typedef list<Info> InfoList;
    InfoList infoList;
    InfoList::const_iterator iter;
    stringstream str;
    unsigned int maxPosWidth = 0, maxAliasWidth = 0,
                 maxRelPosWidth = 0, maxStateWidth = 0;
    string indent(doIndent ? "  " : "");

    m.getMaster(&master);

    lastAlias = 0;
    aliasIndex = 0;
    for (i = 0; i < master.slave_count; i++) {
        m.getSlave(&slave, i);

        if (slave.alias) {
            lastAlias = slave.alias;
            aliasIndex = 0;
        }

        if (slaveInList(slave, slaves)) {
            str << dec << i;
            info.pos = str.str();
            str.clear();
            str.str("");

            str << lastAlias;
            info.alias = str.str();
            str.str("");

            str << aliasIndex;
            info.relPos = str.str();
            str.str("");

            info.state = alStateString(slave.al_state);
            info.flag = (slave.error_flag ? 'E' : '+');
            info.device = slave.device_index;

            if (strlen(slave.name)) {
                info.name = slave.name;
            } else {
                str << "0x" << hex << setfill('0')
                    << setw(8) << slave.vendor_id << ":0x"
                    << setw(8) << slave.product_code;
                info.name = str.str();
                str.str("");
            }


            infoList.push_back(info);

            if (info.pos.length() > maxPosWidth)
                maxPosWidth = info.pos.length();
            if (info.alias.length() > maxAliasWidth)
                maxAliasWidth = info.alias.length();
            if (info.relPos.length() > maxRelPosWidth)
                maxRelPosWidth = info.relPos.length();
            if (info.state.length() > maxStateWidth)
                maxStateWidth = info.state.length();
        }

        aliasIndex++;
    }

    if (infoList.size() && doIndent) {
        cout << "Master" << dec << m.getIndex() << endl;
    }

    lastDevice = EC_DEVICE_MAIN;
    for (iter = infoList.begin(); iter != infoList.end(); iter++) {
        if (iter->device != lastDevice) {
            lastDevice = iter->device;
            cout << "xxx LINK FAILURE xxx" << endl;
        }
        cout << indent << setfill(' ') << right
            << setw(maxPosWidth) << iter->pos << "  "
            << setw(maxAliasWidth) << iter->alias
            << ":" << left
            << setw(maxRelPosWidth) << iter->relPos << "  "
            << setw(maxStateWidth) << iter->state << "  "
            << iter->flag << "  "
            << iter->name << endl;
    }
}

/****************************************************************************/

void CommandSlaves::showSlaves(
        MasterDevice &m,
        const SlaveList &slaves
        )
{
    SlaveList::const_iterator si;
    int i;

    for (si = slaves.begin(); si != slaves.end(); si++) {
        cout << "=== Master " << dec << m.getIndex()
            << ", Slave " << dec << si->position << " ===" << endl;

        if (si->alias)
            cout << "Alias: " << si->alias << endl;

        cout
            << "Device: " << (si->device_index ? "Backup" : "Main") << endl
            << "State: " << alStateString(si->al_state) << endl
            << "Flag: " << (si->error_flag ? 'E' : '+') << endl
            << "Identity:" << endl
            << "  Vendor Id:       0x"
            << hex << setfill('0')
            << setw(8) << si->vendor_id << endl
            << "  Product code:    0x"
            << setw(8) << si->product_code << endl
            << "  Revision number: 0x"
            << setw(8) << si->revision_number << endl
            << "  Serial number:   0x"
            << setw(8) << si->serial_number << endl
            << "SII:" << endl
            << "  Parallel read: ";
        if (si->sii_parallel_words) {
            cout << si->sii_parallel_words << " words";
        }
        else {
            cout << " n/a";
        }
        cout << endl;

        cout << "DL information:" << endl
            << "  FMMU bit operation: "
            << (si->fmmu_bit ? "yes" : "no") << endl
            << "  Distributed clocks: ";
        if (si->dc_supported) {
            if (si->has_dc_system_time) {
                cout << "yes, ";
                switch (si->dc_range) {
                    case EC_DC_32:
                        cout << "32 bit";
                        break;
                    case EC_DC_64:
                        cout << "64 bit";
                        break;
                    default:
                        cout << "???";
                }
                cout << endl;
            } else {
                cout << "yes, delay measurement only" << endl;
            }
            cout << "  DC system time transmission delay: "
                << dec << si->transmission_delay << " ns" << endl;
        } else {
            cout << "no" << endl;
        }

        cout << "Port  Type  Link  Loop    Signal  NextSlave";
        if (si->dc_supported)
            cout << "  RxTime [ns]  Diff [ns]   NextDc [ns]";
        cout << endl;

        for (i = 0; i < EC_MAX_PORTS; i++) {
            cout << "   " << i << "  " << setfill(' ') << left << setw(4);
            switch (si->ports[i].desc) {
                case EC_PORT_NOT_IMPLEMENTED:
                    cout << "N/A";
                    break;
                case EC_PORT_NOT_CONFIGURED:
                    cout << "N/C";
                    break;
                case EC_PORT_EBUS:
                    cout << "EBUS";
                    break;
                case EC_PORT_MII:
                    cout << "MII";
                    break;
                default:
                    cout << "???";
            }

            cout << "  " << setw(4)
                << (si->ports[i].link.link_up ? "up" : "down")
                << "  " << setw(6)
                << (si->ports[i].link.loop_closed ? "closed" : "open")
                << "  " << setw(6)
                << (si->ports[i].link.signal_detected ? "yes" : "no")
                << "  " << setw(9) << right;

            if (si->ports[i].next_slave != 0xffff) {
                cout << si->ports[i].next_slave;
            } else {
                cout << "-";
            }

            if (si->dc_supported) {
                cout << "  " << setw(11) << right;
                if (!si->ports[i].link.loop_closed) {
                    cout << si->ports[i].receive_time;
                } else {
                    cout << "-";
                }
                cout << "  " << setw(10);
                if (!si->ports[i].link.loop_closed) {
                    cout << si->ports[i].receive_time -
                        si->ports[0].receive_time;
                } else {
                    cout << "-";
                }
                cout << "  " << setw(10);
                if (!si->ports[i].link.loop_closed) {
                    cout << si->ports[i].delay_to_next_dc;
                } else {
                    cout << "-";
                }
            }

            cout << endl;
        }

        if (si->mailbox_protocols) {
            list<string> protoList;
            list<string>::const_iterator protoIter;

            cout << "Mailboxes:" << endl
                << "  Bootstrap RX: 0x" << setfill('0')
                << hex << setw(4) << si->boot_rx_mailbox_offset << "/"
                << dec << si->boot_rx_mailbox_size
                << ", TX: 0x"
                << hex << setw(4) << si->boot_tx_mailbox_offset << "/"
                << dec << si->boot_tx_mailbox_size << endl
                << "  Standard  RX: 0x"
                << hex << setw(4) << si->std_rx_mailbox_offset << "/"
                << dec << si->std_rx_mailbox_size
                << ", TX: 0x"
                << hex << setw(4) << si->std_tx_mailbox_offset << "/"
                << dec << si->std_tx_mailbox_size << endl
                << "  Supported protocols: ";

            if (si->mailbox_protocols & EC_MBOX_AOE) {
                protoList.push_back("AoE");
            }
            if (si->mailbox_protocols & EC_MBOX_EOE) {
                protoList.push_back("EoE");
            }
            if (si->mailbox_protocols & EC_MBOX_COE) {
                protoList.push_back("CoE");
            }
            if (si->mailbox_protocols & EC_MBOX_FOE) {
                protoList.push_back("FoE");
            }
            if (si->mailbox_protocols & EC_MBOX_SOE) {
                protoList.push_back("SoE");
            }
            if (si->mailbox_protocols & EC_MBOX_VOE) {
                protoList.push_back("VoE");
            }

            for (protoIter = protoList.begin(); protoIter != protoList.end();
                    protoIter++) {
                if (protoIter != protoList.begin())
                    cout << ", ";
                cout << *protoIter;
            }
            cout << endl;
        }

        if (si->has_general_category) {
            cout << "General:" << endl
                << "  Group: " << si->group << endl
                << "  Image name: " << si->image << endl
                << "  Order number: " << si->order << endl
                << "  Device name: " << si->name << endl;

            if (si->mailbox_protocols & EC_MBOX_COE) {
                cout << "  CoE details:" << endl
                    << "    Enable SDO: "
                    << (si->coe_details.enable_sdo ? "yes" : "no") << endl
                    << "    Enable SDO Info: "
                    << (si->coe_details.enable_sdo_info ? "yes" : "no")
                    << endl
                    << "    Enable PDO Assign: "
                    << (si->coe_details.enable_pdo_assign
                            ? "yes" : "no") << endl
                    << "    Enable PDO Configuration: "
                    << (si->coe_details.enable_pdo_configuration
                            ? "yes" : "no") << endl
                    << "    Enable Upload at startup: "
                    << (si->coe_details.enable_upload_at_startup
                            ? "yes" : "no") << endl
                    << "    Enable SDO complete access: "
                    << (si->coe_details.enable_sdo_complete_access
                            ? "yes" : "no") << endl;
            }

            cout << "  Flags:" << endl
                << "    Enable SafeOp: "
                << (si->general_flags.enable_safeop ? "yes" : "no") << endl
                << "    Enable notLRW: "
                << (si->general_flags.enable_not_lrw ? "yes" : "no") << endl
                << "  Current consumption: "
                << dec << si->current_on_ebus << " mA" << endl;
        }
    }
}

/****************************************************************************/

bool CommandSlaves::slaveInList(
        const ec_ioctl_slave_t &slave,
        const SlaveList &slaves
        )
{
    SlaveList::const_iterator si;

    for (si = slaves.begin(); si != slaves.end(); si++) {
        if (si->position == slave.position) {
            return true;
        }
    }

    return false;
}

/****************************************************************************/

void CommandSlaves::showSlavesJson(
        MasterDevice &m,
        const SlaveList &slaves
        )
{
    SlaveList::const_iterator si;
    int i;
    bool first = true;

    cout << "  {" << endl
        << "    \"master\": " << m.getIndex() << "," << endl
        << "    \"slaves\": [" << endl;

    for (si = slaves.begin(); si != slaves.end(); si++) {
        if (!first) {
            cout << "," << endl;
        }
        first = false;

        cout << "      {" << endl
            << "        \"position\": " << si->position << ","
            << endl
            << "        \"alias\": " << si->alias << "," << endl
            << "        \"device\": \""
            << (si->device_index ? "backup" : "main") << "\"," << endl
            << "        \"state\": \""
            << alStateBaseString(si->al_state) << "\"," << endl
            << "        \"state_error\": "
            << ((si->al_state & EC_SLAVE_STATE_ACK_ERR) ? "true" : "false")
            << "," << endl
            << "        \"error\": "
            << (si->error_flag ? "true" : "false") << "," << endl
            << "        \"name\": \"" << jsonEscape(si->name) << "\","
            << endl
            << "        \"identity\": {" << endl
            << "          \"vendor_id\": " << si->vendor_id << ","
            << endl
            << "          \"product_code\": " << si->product_code << ","
            << endl
            << "          \"revision_number\": " << si->revision_number
            << "," << endl
            << "          \"serial_number\": " << si->serial_number << endl
            << "        }," << endl
            << "        \"sii_parallel_read_words\": ";
        if (si->sii_parallel_words) {
            cout << si->sii_parallel_words;
        } else {
            cout << "null";
        }
        cout << "," << endl
            << "        \"fmmu_bit_operation\": "
            << (si->fmmu_bit ? "true" : "false") << "," << endl
            << "        \"distributed_clocks\": {" << endl
            << "          \"support\": \"";
        if (!si->dc_supported) {
            cout << "no";
        } else if (!si->has_dc_system_time) {
            cout << "delay_measurement_only";
        } else {
            cout << "full";
        }
        cout << "\"";
        if (si->dc_supported && si->has_dc_system_time) {
            cout << "," << endl
                << "          \"range_bits\": "
                << (si->dc_range == EC_DC_64 ? 64 : 32);
        }
        if (si->dc_supported) {
            cout << "," << endl
                << "          \"transmission_delay_ns\": "
                << si->transmission_delay;
        }
        cout << endl
            << "        }," << endl
            << "        \"ports\": [" << endl;

        for (i = 0; i < EC_MAX_PORTS; i++) {
            const char *type;
            switch (si->ports[i].desc) {
                case EC_PORT_NOT_IMPLEMENTED: type = "N/A"; break;
                case EC_PORT_NOT_CONFIGURED:  type = "N/C"; break;
                case EC_PORT_EBUS:            type = "EBUS"; break;
                case EC_PORT_MII:             type = "MII"; break;
                default:                      type = "???";
            }

            cout << "          {" << endl
                << "            \"index\": " << i << "," << endl
                << "            \"type\": \"" << type << "\"," << endl
                << "            \"link_up\": "
                << (si->ports[i].link.link_up ? "true" : "false") << ","
                << endl
                << "            \"loop_closed\": "
                << (si->ports[i].link.loop_closed ? "true" : "false") << ","
                << endl
                << "            \"signal_detected\": "
                << (si->ports[i].link.signal_detected ? "true" : "false")
                << "," << endl
                << "            \"next_slave\": ";
            if (si->ports[i].next_slave != 0xffff) {
                cout << si->ports[i].next_slave;
            } else {
                cout << "null";
            }

            if (si->dc_supported) {
                cout << "," << endl
                    << "            \"receive_time_ns\": ";
                if (!si->ports[i].link.loop_closed) {
                    cout << si->ports[i].receive_time;
                } else {
                    cout << "null";
                }
                cout << "," << endl
                    << "            \"diff_ns\": ";
                if (!si->ports[i].link.loop_closed) {
                    cout << si->ports[i].receive_time -
                        si->ports[0].receive_time;
                } else {
                    cout << "null";
                }
                cout << "," << endl
                    << "            \"next_dc_ns\": ";
                if (!si->ports[i].link.loop_closed) {
                    cout << si->ports[i].delay_to_next_dc;
                } else {
                    cout << "null";
                }
            }

            cout << endl
                << "          }";
            if (i + 1 < EC_MAX_PORTS) {
                cout << ",";
            }
            cout << endl;
        }
        cout << "        ]";

        if (si->mailbox_protocols) {
            bool firstProto = true;

            cout << "," << endl
                << "        \"mailboxes\": {" << endl
                << "          \"bootstrap\": {"
                << "\"rx_offset\": " << si->boot_rx_mailbox_offset
                << ", "
                << "\"rx_size\": " << si->boot_rx_mailbox_size << ", "
                << "\"tx_offset\": " << si->boot_tx_mailbox_offset << ", "
                << "\"tx_size\": " << si->boot_tx_mailbox_size
                << "}," << endl
                << "          \"standard\": {"
                << "\"rx_offset\": " << si->std_rx_mailbox_offset << ", "
                << "\"rx_size\": " << si->std_rx_mailbox_size << ", "
                << "\"tx_offset\": " << si->std_tx_mailbox_offset << ", "
                << "\"tx_size\": " << si->std_tx_mailbox_size
                << "}," << endl
                << "          \"protocols\": [";

            static const struct {
                uint16_t flag;
                const char *name;
            } protocols[] = {
                {EC_MBOX_AOE, "AoE"},
                {EC_MBOX_EOE, "EoE"},
                {EC_MBOX_COE, "CoE"},
                {EC_MBOX_FOE, "FoE"},
                {EC_MBOX_SOE, "SoE"},
                {EC_MBOX_VOE, "VoE"},
            };
            unsigned int p;
            for (p = 0; p < sizeof(protocols) / sizeof(protocols[0]); p++) {
                if (si->mailbox_protocols & protocols[p].flag) {
                    if (!firstProto) {
                        cout << ", ";
                    }
                    cout << "\"" << protocols[p].name << "\"";
                    firstProto = false;
                }
            }
            cout << "]" << endl
                << "        }";
        }

        if (si->has_general_category) {
            cout << "," << endl
                << "        \"general\": {" << endl
                << "          \"group\": \"" << jsonEscape(si->group)
                << "\"," << endl
                << "          \"image\": \"" << jsonEscape(si->image)
                << "\"," << endl
                << "          \"order\": \"" << jsonEscape(si->order)
                << "\"," << endl
                << "          \"device_name\": \"" << jsonEscape(si->name)
                << "\"";

            if (si->mailbox_protocols & EC_MBOX_COE) {
                cout << "," << endl
                    << "          \"coe_details\": {" << endl
                    << "            \"enable_sdo\": "
                    << (si->coe_details.enable_sdo ? "true" : "false") << ","
                    << endl
                    << "            \"enable_sdo_info\": "
                    << (si->coe_details.enable_sdo_info ? "true" : "false")
                    << "," << endl
                    << "            \"enable_pdo_assign\": "
                    << (si->coe_details.enable_pdo_assign
                            ? "true" : "false") << "," << endl
                    << "            \"enable_pdo_configuration\": "
                    << (si->coe_details.enable_pdo_configuration
                            ? "true" : "false") << "," << endl
                    << "            \"enable_upload_at_startup\": "
                    << (si->coe_details.enable_upload_at_startup
                            ? "true" : "false") << "," << endl
                    << "            \"enable_sdo_complete_access\": "
                    << (si->coe_details.enable_sdo_complete_access
                            ? "true" : "false") << endl
                    << "          }";
            }

            cout << "," << endl
                << "          \"flags\": {" << endl
                << "            \"enable_safeop\": "
                << (si->general_flags.enable_safeop ? "true" : "false")
                << "," << endl
                << "            \"enable_not_lrw\": "
                << (si->general_flags.enable_not_lrw ? "true" : "false")
                << endl
                << "          }," << endl
                << "          \"current_on_ebus_ma\": "
                << si->current_on_ebus << endl
                << "        }";
        }

        cout << endl
            << "      }";
    }

    cout << endl
        << "    ]" << endl
        << "  }";
}

/****************************************************************************/
