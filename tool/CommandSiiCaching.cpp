/*****************************************************************************
 *
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

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdlib>
using namespace std;

#include "CommandSiiCaching.h"
#include "MasterDevice.h"

/****************************************************************************/

CommandCache::CommandCache():
    Command("sii_caching", "Configure SII_CACHING settings.")
{
}

/****************************************************************************/

string CommandCache::helpString(const string &binaryBaseName) const
{
    stringstream str;

    str << binaryBaseName << " " << getName() << " <FIELDS>" << endl
        << endl
        << getBriefDescription() << endl
        << endl
        << "This command configures cache level to speed up scanning process" << endl
        << endl
        << "Caching fields are specified as a decimal number that is a" << endl
        << "combination of the following flags:" << endl
        << "  0 - Disable SII caching" << endl
        << "  1 - Use vendor ID" << endl
        << "  2 - Use product code" << endl
        << "  4 - Use revision number" << endl
        << "  8 - Use serial number" << endl
        << " 16 - Use alias address" << endl
        << endl
        << "Example: 'ethercat cache 31' enables caching based on all" << endl
        << "  five fields (1+2+4+8+16=31)." << endl
        << "Example: 'ethercat cache 0' disables SII caching." << endl
        << endl
        << "Arguments:" << endl
        << "  FIELDS  Decimal number representing the caching fields." << endl
        << endl;

    return str.str();
}

/****************************************************************************/

void CommandCache::execute(const StringVector &args)
{
    MasterIndexList masterIndices;
    uint32_t fields;
    stringstream err;

    if (args.size() != 1) 
    {
        err << "'" << getName() << "' requires exactly one argument!";
        throwInvalidUsageException(err);
    }

    // Use strtoul instead of stoul for older C++ standards
    char *endptr;
    fields = strtoul(args[0].c_str(), &endptr, 10);

    // Basic error checking to ensure the string was actually a number
    if (*endptr != '\0' || args[0].empty()) {
        err << "Invalid caching fields value '" << args[0] << "'";
        throwInvalidUsageException(err);
    }

    if (fields > 31) {
        err << "Caching fields value must be between 0 and 31.";
        throwInvalidUsageException(err);
    }

    masterIndices = getMasterIndices();
    MasterIndexList::const_iterator mi;
    for (mi = masterIndices.begin(); mi != masterIndices.end(); mi++) {
        MasterDevice m(*mi);
        m.open(MasterDevice::ReadWrite);
        
        try {
            m.setSiiCaching(fields);
            cout << "Master " << dec << *mi << ": SII caching set to 0x"
                << hex << setfill('0') << setw(2) << fields << dec << endl;
        } catch (const MasterDeviceException &e) {
            err << "Master " << *mi << ": " << e.what();
            throwCommandException(err);
        }
    }
}

/****************************************************************************/
