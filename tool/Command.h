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

#ifndef __COMMAND_H__
#define __COMMAND_H__

#include <stdexcept>
#include <vector>
#include <list>
#include <sstream>

#include "../master/ioctl.h"

class MasterDevice;

/****************************************************************************/

class InvalidUsageException:
    public std::runtime_error
{
    friend class Command;

    protected:
        /** Constructor with stringstream parameter. */
        InvalidUsageException(
                const std::stringstream &s): /**< Message. */
            std::runtime_error(s.str()) {}
};

/****************************************************************************/

class CommandException:
    public std::runtime_error
{
    friend class Command;

    protected:
        /** Constructor with char * parameter. */
        CommandException(
                const std::string &msg): /**< Message. */
            std::runtime_error(msg) {}

        /** Constructor with stringstream parameter. */
        CommandException(
                const std::stringstream &s): /**< Message. */
            std::runtime_error(s.str()) {}
};

/****************************************************************************/

class Command
{
    public:
        Command(const std::string &, const std::string &);
        virtual ~Command();

        const std::string &getName() const;
        const std::string &getBriefDescription() const;

        typedef std::list<unsigned int> MasterIndexList;
        void setMasters(const std::string &);
        MasterIndexList getMasterIndices() const;
        unsigned int getSingleMasterIndex() const;

        enum Verbosity {
            Quiet,
            Normal,
            Verbose
        };
        void setVerbosity(Verbosity);
        Verbosity getVerbosity() const;

        void setAliases(const std::string &);
        void setPositions(const std::string &);

        void setDomains(const std::string &);
        typedef std::list<unsigned int> DomainIndexList;
        DomainIndexList getDomainIndices() const;

        void setDataType(const std::string &);
        const std::string &getDataType() const;

        void setEmergency(bool);
        bool getEmergency() const;

        void setForce(bool);
        bool getForce() const;

        void setOutputFile(const std::string &);
        const std::string &getOutputFile() const;

        void setSkin(const std::string &);
        const std::string &getSkin() const;

        bool matchesSubstr(const std::string &) const;
        bool matchesAbbrev(const std::string &) const;

        virtual std::string helpString(const std::string &) const = 0;

        typedef std::vector<std::string> StringVector;
        virtual void execute(const StringVector &) = 0;

        static std::string numericInfo();

    protected:
        enum {BreakAfterBytes = 16};

        void throwInvalidUsageException(const std::stringstream &) const;
        void throwCommandException(const std::string &) const;
        void throwCommandException(const std::stringstream &) const;
        void throwSingleSlaveRequired(unsigned int) const;

        typedef std::list<ec_ioctl_slave_t> SlaveList;
        SlaveList selectedSlaves(MasterDevice &);
        typedef std::list<ec_ioctl_config_t> ConfigList;
        ConfigList selectedConfigs(MasterDevice &);
        typedef std::list<ec_ioctl_domain_t> DomainList;
        DomainList selectedDomains(MasterDevice &, const ec_ioctl_master_t &);
        int emergencySlave() const;

        static std::string alStateString(uint8_t);

    private:
        std::string name;
        std::string briefDesc;
        std::string masters;
        Verbosity verbosity;
        std::string aliases;
        std::string positions;
        std::string domains;
        std::string dataType;
        bool emergency;
        bool force;
        std::string outputFile;
        std::string skin;

        Command();
};

/****************************************************************************/

inline const std::string &Command::getName() const
{
    return name;
}

/****************************************************************************/

inline const std::string &Command::getBriefDescription() const
{
    return briefDesc;
}

/****************************************************************************/

inline Command::Verbosity Command::getVerbosity() const
{
    return verbosity;
}

/****************************************************************************/

inline const std::string &Command::getDataType() const
{
    return dataType;
}

/****************************************************************************/

inline bool Command::getEmergency() const
{
    return emergency;
}

/****************************************************************************/

inline bool Command::getForce() const
{
    return force;
}

/****************************************************************************/

inline const std::string &Command::getOutputFile() const
{
    return outputFile;
}

/****************************************************************************/

inline const std::string &Command::getSkin() const
{
    return skin;
}

/****************************************************************************/

#endif
