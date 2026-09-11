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

#ifndef __COMMANDCONFIG_H__
#define __COMMANDCONFIG_H__

#include <list>

#include "Command.h"
#include "SoeCommand.h"

/****************************************************************************/

class CommandConfig:
    public Command,
    public SoeCommand
{
    public:
        CommandConfig();

        std::string helpString(const std::string &) const;
        void execute(const StringVector &);

    protected:
        struct Info {
            std::string alias;
            std::string pos;
            std::string ident;
            std::string slavePos;
            std::string state;
        };

        void showDetailedConfigs(MasterDevice &, const ConfigList &, bool);
        void listConfigs(MasterDevice &m, const ConfigList &, bool);
        void showConfigsJson(MasterDevice &, const ConfigList &);
};

/****************************************************************************/

#endif
