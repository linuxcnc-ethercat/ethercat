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
 *  vim: expandtab
 *
 ****************************************************************************/

#include "CommandData.h"

#include "MasterDevice.h"

#include <iostream>

using std::string;
using std::stringstream;
using std::endl;
using std::cout;

/****************************************************************************/

CommandData::CommandData():
    Command("data", "Output binary domain process data.")
{
}

/****************************************************************************/

string CommandData::helpString(const string &binaryBaseName) const
{
    stringstream str;

    str << binaryBaseName << " " << getName() << " [OPTIONS]" << endl
        << endl
        << getBriefDescription() << endl
        << endl
        << "Data of multiple domains are concatenated." << endl
        << endl
        << "The global --json option outputs, per domain, the" << endl
        << "participating slave configurations/FMMUs together with" << endl
        << "their process data, instead of the raw concatenated bytes."
        << endl
        << endl
        << "Command-specific options:" << endl
        << "  --domain -d <index>  Positive numerical domain index." << endl
        << "                       If omitted, data of all domains" << endl
        << "                       are output." << endl
        << endl
        << numericInfo();

    return str.str();
}

/****************************************************************************/

void CommandData::execute(const StringVector &args)
{
    MasterIndexList masterIndices;
    DomainList domains;
    DomainList::const_iterator di;
    bool firstMaster = true;

    if (args.size()) {
        stringstream err;
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
        ec_ioctl_master_t io;
        MasterDevice m(*mi);
        m.open(MasterDevice::Read);
        m.getMaster(&io);

        domains = selectedDomains(m, io);

        if (getJson()) {
            bool firstDomain = true;

            if (!firstMaster) {
                cout << "," << endl;
            }
            firstMaster = false;

            cout << "  {" << endl
                << "    \"master\": " << m.getIndex() << "," << endl
                << "    \"domains\": [" << endl;

            for (di = domains.begin(); di != domains.end(); di++) {
                if (!firstDomain) {
                    cout << "," << endl;
                }
                firstDomain = false;
                outputDomainDataJson(m, *di);
            }

            cout << endl
                << "    ]" << endl
                << "  }";
            continue;
        }

        for (di = domains.begin(); di != domains.end(); di++) {
            outputDomainData(m, *di);
        }
    }

    if (getJson()) {
        cout << endl << "]" << endl;
    }
}

/****************************************************************************/

void CommandData::outputDomainData(
        MasterDevice &m,
        const ec_ioctl_domain_t &domain
        )
{
    ec_ioctl_domain_data_t data;
    unsigned char *processData;
    unsigned int i;

    if (!domain.data_size)
        return;

    processData = new unsigned char[domain.data_size];

    try {
        m.getData(&data, domain.index, domain.data_size, processData);
    } catch (MasterDeviceException &e) {
        delete [] processData;
        throw e;
    }

    for (i = 0; i < data.data_size; i++)
        cout << processData[i];
    cout.flush();

    delete [] processData;
}

/****************************************************************************/

void CommandData::outputDomainDataJson(
        MasterDevice &m,
        const ec_ioctl_domain_t &domain
        )
{
    ec_ioctl_domain_data_t data;
    unsigned char *processData;
    ec_ioctl_domain_fmmu_t fmmu;
    unsigned int i, j, dataOffset;
    bool firstFmmu = true;

    cout << "      {" << endl
        << "        \"index\": " << domain.index << "," << endl
        << "        \"size\": " << domain.data_size << "," << endl
        << "        \"fmmus\": [";

    if (!domain.data_size) {
        cout << "]" << endl
            << "      }";
        return;
    }

    processData = new unsigned char[domain.data_size];

    try {
        m.getData(&data, domain.index, domain.data_size, processData);
    } catch (MasterDeviceException &e) {
        delete [] processData;
        throw e;
    }

    for (i = 0; i < domain.fmmu_count; i++) {
        m.getFmmu(&fmmu, domain.index, i);

        dataOffset = fmmu.logical_address - domain.logical_base_address;
        if (dataOffset + fmmu.data_size > domain.data_size) {
            delete [] processData;
            stringstream err;
            err << "Fmmu information corrupted!";
            throwCommandException(err);
        }

        if (!firstFmmu) {
            cout << ",";
        }
        firstFmmu = false;

        cout << endl << "          {" << endl
            << "            \"slave_config_alias\": "
            << fmmu.slave_config_alias << "," << endl
            << "            \"slave_config_position\": "
            << fmmu.slave_config_position << "," << endl
            << "            \"sync_manager\": "
            << (unsigned int) fmmu.sync_index << "," << endl
            << "            \"direction\": \""
            << (fmmu.dir == EC_DIR_INPUT ? "input" : "output") << "\","
            << endl
            << "            \"logical_address\": "
            << fmmu.logical_address << "," << endl
            << "            \"size\": " << fmmu.data_size << "," << endl
            << "            \"data\": [";

        for (j = 0; j < fmmu.data_size; j++) {
            if (j) {
                cout << ", ";
            }
            cout << (unsigned int) *(processData + dataOffset + j);
        }

        cout << "]" << endl
            << "          }";
    }

    delete [] processData;

    if (!firstFmmu) {
        cout << endl << "        ";
    }
    cout << "]" << endl
        << "      }";
}

/****************************************************************************/
