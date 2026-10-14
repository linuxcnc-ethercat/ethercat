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

#include "Command.h"

#include "NumberListParser.h"
#include "MasterDevice.h"

#include <iostream>
#include <iomanip>
#include <map>
#include <climits> // MB_LEN_MAX
#include <clocale>
#include <cstring>
#include <cwchar>
#include <langinfo.h>

using std::string;
using std::stringstream;
using std::cout;
using std::endl;
using std::map;

/****************************************************************************/

typedef map<uint16_t, ec_ioctl_config_t> AliasMap;
typedef map<uint16_t, AliasMap> ConfigMap;

/****************************************************************************/

class MasterIndexParser:
    public NumberListParser
{
    protected:
        int getMax() {
            MasterDevice dev;
            dev.setIndex(0U);
            dev.open(MasterDevice::Read);
            return (int) dev.getMasterCount() - 1;
        };
};

/****************************************************************************/

class SlaveAliasParser:
    public NumberListParser
{
    public:
        SlaveAliasParser(ec_ioctl_master_t &master, MasterDevice &dev):
            master(master), dev(dev) {}

    protected:
        int getMax() {
            unsigned int i;

            uint16_t maxAlias = 0;
            for (i = 0; i < master.slave_count; i++) {
                ec_ioctl_slave_t slave;
                dev.getSlave(&slave, i);
                if (slave.alias > maxAlias) {
                    maxAlias = slave.alias;
                }
            }
            return maxAlias ? maxAlias : -1;
        };

    private:
        ec_ioctl_master_t &master;
        MasterDevice &dev;
};

/****************************************************************************/

class ConfigAliasParser:
    public NumberListParser
{
    public:
        explicit ConfigAliasParser(unsigned int maxAlias):
            maxAlias(maxAlias) {}

    protected:
        int getMax() { return maxAlias; };

    private:
        unsigned int maxAlias;
};

/****************************************************************************/

class PositionParser:
    public NumberListParser
{
    public:
        explicit PositionParser(unsigned int count):
            count(count) {}

    protected:
        int getMax() {
            return count - 1;
        };

    private:
        const unsigned int count;
};

/****************************************************************************/

class AliasPositionParser:
    public NumberListParser
{
    public:
        explicit AliasPositionParser(const AliasMap &aliasMap):
            aliasMap(aliasMap) {}

    protected:
        int getMax() {
            AliasMap::const_iterator i;
            int maxPos = -1;

            for (i = aliasMap.begin(); i != aliasMap.end(); i++) {
                if (i->first > maxPos) {
                    maxPos = i->first;
                }
            }

            return maxPos;
        };

    private:
        const AliasMap &aliasMap;
};

/****************************************************************************/

Command::Command(const string &name, const string &briefDesc):
    name(name),
    briefDesc(briefDesc),
    verbosity(Normal),
    emergency(false),
    force(false),
    json(false)
{
}

/****************************************************************************/

Command::~Command()
{
}

/****************************************************************************/

void Command::setMasters(const string &m)
{
    masters = m;
};

/****************************************************************************/

void Command::setVerbosity(Verbosity v)
{
    verbosity = v;
};

/****************************************************************************/

void Command::setAliases(const string &a)
{
    aliases = a;
};

/****************************************************************************/

void Command::setPositions(const string &p)
{
    positions = p;
};

/****************************************************************************/

void Command::setDomains(const string &d)
{
    domains = d;
};

/****************************************************************************/

void Command::setDataType(const string &t)
{
    dataType = t;
};

/****************************************************************************/

void Command::setEmergency(bool e)
{
    emergency = e;
};

/****************************************************************************/

void Command::setForce(bool f)
{
    force = f;
};

/****************************************************************************/

void Command::setOutputFile(const string &f)
{
    outputFile = f;
};

/****************************************************************************/

void Command::setSkin(const string &s)
{
    skin = s;
};

/****************************************************************************/

void Command::setJson(bool j)
{
    json = j;
};

/****************************************************************************/

bool Command::matchesSubstr(const string &cmd) const
{
    return name.substr(0, cmd.length()) == cmd;
}

/****************************************************************************/

bool Command::matchesAbbrev(const string &abb) const
{
    unsigned int i;
    size_t pos = 0;

    for (i = 0; i < abb.length(); i++) {
        pos = name.find(abb[i], pos);
        if (pos == string::npos)
            return false;
    }

    return true;
}

/****************************************************************************/

string Command::numericInfo()
{
    stringstream str;

    str << "Numerical values can be specified either with decimal (no" << endl
        << "prefix), octal (prefix '0') or hexadecimal (prefix '0x') base."
        << endl;

    return str.str();
}

/****************************************************************************/

void Command::throwInvalidUsageException(const stringstream &s) const
{
    throw InvalidUsageException(s);
}

/****************************************************************************/

void Command::throwCommandException(const string &msg) const
{
    throw CommandException(msg);
}

/****************************************************************************/

void Command::throwCommandException(const stringstream &s) const
{
    throw CommandException(s);
}

/****************************************************************************/

void Command::throwSingleSlaveRequired(unsigned int size) const
{
    stringstream err;

    err << "The slave selection matches " << size << " slaves. '"
        << name << "' requires a single slave.";

    throwInvalidUsageException(err);
}

/****************************************************************************/

Command::MasterIndexList Command::getMasterIndices() const
{
    MasterIndexList indices;

    try {
        MasterIndexParser p;
        indices = p.parse(masters.c_str());
    } catch (MasterDeviceException &e) {
        stringstream err;
        err << "Failed to obtain number of masters: " << e.what();
        throwCommandException(err);
    } catch (std::runtime_error &e) {
        stringstream err;
        err << "Invalid master argument '" << masters << "': " << e.what();
        throwInvalidUsageException(err);
    }

    return indices;
}

/****************************************************************************/

unsigned int Command::getSingleMasterIndex() const
{
    MasterIndexList masterIndices = getMasterIndices();

    if (masterIndices.size() != 1) {
        stringstream err;
        err << getName() << " requires to select a single master!";
        throwInvalidUsageException(err);
    }

    return masterIndices.front();
}

/****************************************************************************/

Command::SlaveList Command::selectedSlaves(MasterDevice &m)
{
    ec_ioctl_master_t master;
    unsigned int i;
    ec_ioctl_slave_t slave;
    SlaveList list;

    m.getMaster(&master);

    if (aliases == "-") { // no alias given
        PositionParser pp(master.slave_count);
        NumberListParser::List posList = pp.parse(positions.c_str());
        NumberListParser::List::const_iterator pi;

        for (pi = posList.begin(); pi != posList.end(); pi++) {
            if (*pi < master.slave_count) {
                m.getSlave(&slave, *pi);
                list.push_back(slave);
            }
        }
    } else { // aliases given
        SlaveAliasParser ap(master, m);
        NumberListParser::List aliasList = ap.parse(aliases.c_str());
        NumberListParser::List::const_iterator ai;

        for (ai = aliasList.begin(); ai != aliasList.end(); ai++) {
            // gather slaves with that alias (and following)
            uint16_t lastAlias = 0;
            std::vector<ec_ioctl_slave_t> aliasSlaves;

            for (i = 0; i < master.slave_count; i++) {
                m.getSlave(&slave, i);
                if (slave.alias) {
                    if (lastAlias && lastAlias == *ai && slave.alias != *ai) {
                        // ignore multiple ocurrences of the same alias to
                        // assure consistency for the position argument
                        break;
                    }
                    lastAlias = slave.alias;
                }
                if (lastAlias == *ai) {
                    aliasSlaves.push_back(slave);
                }
            }

            PositionParser pp(aliasSlaves.size());
            NumberListParser::List posList = pp.parse(positions.c_str());
            NumberListParser::List::const_iterator pi;

            for (pi = posList.begin(); pi != posList.end(); pi++) {
                if (*pi < aliasSlaves.size()) {
                    list.push_back(aliasSlaves[*pi]);
                }
            }
        }
    }

    return list;
}

/****************************************************************************/

bool operator<(
        const ec_ioctl_config_t &a,
        const ec_ioctl_config_t &b
        )
{
    return a.alias < b.alias
        || (a.alias == b.alias && a.position < b.position);
}

/****************************************************************************/

Command::ConfigList Command::selectedConfigs(MasterDevice &m)
{
    unsigned int i;
    ec_ioctl_master_t master;
    ec_ioctl_config_t config;
    ConfigList list;
    stringstream err;

    m.getMaster(&master);

    if (aliases == "-" && positions == "-") { // shortcut
        for (i = 0; i < master.config_count; i++) {
            m.getConfig(&config, i);
            list.push_back(config);
        }
    } else { // take the long way home...
        ConfigMap configs;
        uint16_t maxAlias = 0;

        // fill cascaded map structure with all configs
        for (i = 0; i < master.config_count; i++) {
            m.getConfig(&config, i);
            AliasMap &aliasMap = configs[config.alias];
            aliasMap[config.position] = config;
            if (config.alias > maxAlias) {
                maxAlias = config.alias;
            }
        }

        ConfigAliasParser ap(maxAlias);
        NumberListParser::List aliasList = ap.parse(aliases.c_str());
        NumberListParser::List::const_iterator ai;

        for (ai = aliasList.begin(); ai != aliasList.end(); ai++) {
            ConfigMap::iterator ci = configs.find(*ai);
            if (ci == configs.end()) {
                continue;
            }

            AliasMap &aliasMap = configs[*ai];
            AliasPositionParser pp(aliasMap);
            NumberListParser::List posList = pp.parse(positions.c_str());
            NumberListParser::List::const_iterator pi;

            for (pi = posList.begin(); pi != posList.end(); pi++) {
                AliasMap::const_iterator ci;

                ci = aliasMap.find(*pi);
                if (ci != aliasMap.end()) {
                    list.push_back(ci->second);
                }
            }
        }
    }

    list.sort();
    return list;
}

/****************************************************************************/

Command::DomainList Command::selectedDomains(MasterDevice &m,
        const ec_ioctl_master_t &io)
{
    DomainList list;

    PositionParser pp(io.domain_count);
    NumberListParser::List domList = pp.parse(domains.c_str());
    NumberListParser::List::const_iterator di;

    for (di = domList.begin(); di != domList.end(); di++) {
        if (*di < io.domain_count) {
            ec_ioctl_domain_t d;
            m.getDomain(&d, *di);
            list.push_back(d);
        }
    }

    return list;
}

/****************************************************************************/

int Command::emergencySlave() const
{
    unsigned int ret;

    stringstream str;
    str << positions;
    str >> ret;

    return ret;
}

/****************************************************************************/

string Command::alStateString(uint8_t state)
{
    string ret(alStateBaseString(state));

    if (state & EC_SLAVE_STATE_ACK_ERR) {
        ret += "+ERROR";
    }

    return ret;
}

/****************************************************************************/

string Command::alStateBaseString(uint8_t state)
{
    switch (state & EC_SLAVE_STATE_MASK) {
        case 1: return "INIT";
        case 2: return "PREOP";
        case 3: return "BOOT";
        case 4: return "SAFEOP";
        case 8: return "OP";
        default: return "???";
    }
}

/****************************************************************************/

string Command::directionString(ec_direction_t dir)
{
    switch (dir) {
        case EC_DIR_OUTPUT: return "output";
        case EC_DIR_INPUT: return "input";
        default: return "invalid";
    }
}

/****************************************************************************/

/** Escapes a string for use in a JSON string literal.
 *
 * The input is passed through toUtf8() first, so that the result is always
 * valid UTF-8, as required by JSON.
 */
string Command::jsonEscape(const string &raw)
{
    const string in(toUtf8(raw));
    stringstream out;

    for (string::const_iterator c = in.begin(); c != in.end(); c++) {
        switch (*c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if ((unsigned char) *c < 0x20) {
                    out << "\\u" << std::hex << std::setfill('0')
                        << std::setw(4) << (unsigned int) (unsigned char) *c
                        << std::dec;
                } else {
                    out << *c;
                }
        }
    }

    return out.str();
}

/****************************************************************************/

/** Escapes a string for use as XML character data.
 *
 * The input is passed through toUtf8() first, so that the result matches
 * the default XML encoding UTF-8.
 */
string Command::xmlEscape(const string &raw)
{
    const string in(toUtf8(raw));
    string out;

    for (string::const_iterator c = in.begin(); c != in.end(); c++) {
        switch (*c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out += *c;
        }
    }

    return out;
}

/****************************************************************************/

/** Unicode code points of the Windows-1252 characters 0x80 to 0x9F.
 *
 * The five positions undefined in Windows-1252 map to the C1 control codes,
 * as in ISO 8859-1.
 */
static const wchar_t cp1252Table[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

/****************************************************************************/

/** Strictly decodes UTF-8.
 *
 * Overlong forms, surrogates and code points beyond U+10FFFF are rejected.
 *
 * \return false, if the input is not valid UTF-8.
 */
static bool decodeUtf8(const string &in, std::wstring &out)
{
    size_t i = 0, len = in.size();

    out.clear();

    while (i < len) {
        unsigned char c = in[i];
        unsigned int n, k;
        uint32_t cp, min;

        if (c < 0x80) {
            out += (wchar_t) c;
            i++;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            n = 1;
            cp = c & 0x1F;
            min = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            n = 2;
            cp = c & 0x0F;
            min = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            n = 3;
            cp = c & 0x07;
            min = 0x10000;
        } else {
            return false;
        }

        if (i + n >= len) {
            return false;
        }

        for (k = 1; k <= n; k++) {
            unsigned char cc = in[i + k];
            if ((cc & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (cc & 0x3F);
        }

        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }

        out += (wchar_t) cp;
        i += n + 1;
    }

    return true;
}

/****************************************************************************/

/** Decodes a string read from a slave (SII, CoE) into code points.
 *
 * The encoding of these strings is not specified. If the string is valid
 * UTF-8, it is taken as UTF-8, otherwise as Windows-1252 (a superset of the
 * printable ISO 8859-1 characters, which many slaves use). Latin-1 text
 * containing non-ASCII characters is practically never valid UTF-8, so this
 * heuristic is reliable.
 */
static std::wstring decodeSlaveString(const string &raw)
{
    std::wstring out;

    if (decodeUtf8(raw, out)) {
        return out;
    }

    out.clear();

    for (string::const_iterator c = raw.begin(); c != raw.end(); c++) {
        unsigned char b = *c;
        if (b >= 0x80 && b < 0xA0) {
            out += cp1252Table[b - 0x80];
        } else {
            out += (wchar_t) b;
        }
    }

    return out;
}

/****************************************************************************/

/** Encodes code points as UTF-8.
 */
static string encodeUtf8(const std::wstring &in)
{
    string out;

    for (std::wstring::const_iterator w = in.begin(); w != in.end(); w++) {
        uint32_t cp = *w;

        if (cp < 0x80) {
            out += (char) cp;
        } else if (cp < 0x800) {
            out += (char) (0xC0 | (cp >> 6));
            out += (char) (0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char) (0xE0 | (cp >> 12));
            out += (char) (0x80 | ((cp >> 6) & 0x3F));
            out += (char) (0x80 | (cp & 0x3F));
        } else {
            out += (char) (0xF0 | (cp >> 18));
            out += (char) (0x80 | ((cp >> 12) & 0x3F));
            out += (char) (0x80 | ((cp >> 6) & 0x3F));
            out += (char) (0x80 | (cp & 0x3F));
        }
    }

    return out;
}

/****************************************************************************/

/** Converts a string read from a slave to UTF-8.
 *
 * Use this for machine-readable output (JSON, XML, C code, etc.).
 *
 * \see decodeSlaveString()
 */
string Command::toUtf8(const string &raw)
{
    return encodeUtf8(decodeSlaveString(raw));
}

/****************************************************************************/

/** Converts a string read from a slave to the encoding of the locale.
 *
 * Use this for human-readable output. Requires setlocale() to have been
 * called. In the "C" or "POSIX" locale, which usually means that no locale
 * was configured at all, UTF-8 is output. Characters that can not be
 * represented are replaced by '?'.
 *
 * \see decodeSlaveString()
 */
string Command::toLocal(
        const string &raw, /**< Raw string. */
        size_t maxChars /**< Maximum number of characters. */
        )
{
    std::wstring in(decodeSlaveString(raw).substr(0, maxChars));
    const char *locale = setlocale(LC_CTYPE, NULL);

    if (!locale || !strcmp(locale, "C") || !strcmp(locale, "POSIX")
            || !strcmp(nl_langinfo(CODESET), "UTF-8")) {
        return encodeUtf8(in);
    }

    string out;
    char buf[MB_LEN_MAX];
    mbstate_t state;
    size_t n;

    memset(&state, 0, sizeof(state));

    for (std::wstring::const_iterator w = in.begin(); w != in.end(); w++) {
        n = wcrtomb(buf, *w, &state);
        if (n == (size_t) -1) {
            out += '?';
            memset(&state, 0, sizeof(state));
        } else {
            out.append(buf, n);
        }
    }

    // return to the initial shift state
    n = wcrtomb(buf, L'\0', &state);
    if (n != (size_t) -1 && n > 1) {
        out.append(buf, n - 1);
    }

    return out;
}

/****************************************************************************/

/** Outputs the opening of a JSON document.
 *
 * The document is an object containing the format version, the EtherCAT
 * master version and revision and a list with the given key. The list
 * elements have to be indented by four spaces.
 */
void Command::jsonBegin(const string &listKey)
{
    cout << "{" << endl
        << "  \"json_version\": " << JsonFormatVersion << "," << endl
        << "  \"ethercat_version\": \"" << jsonEscape(VERSION) << "\","
        << endl
        << "  \"ethercat_revision\": \"" << jsonEscape(EC_STR(REV)) << "\","
        << endl
        << "  \"" << listKey << "\": [" << endl;
}

/****************************************************************************/

/** Outputs the closing of a JSON document opened with jsonBegin().
 */
void Command::jsonEnd()
{
    cout << endl << "  ]" << endl << "}" << endl;
}

/****************************************************************************/
