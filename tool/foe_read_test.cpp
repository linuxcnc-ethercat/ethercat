/*****************************************************************************
 *
 *  Copyright (C) 2026 StitchLilo626
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

#include "CommandFoeRead.h"
#include "MasterDevice.h"
#include "foe.h"

#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

static const char bytes[] = {
    0, 'A', static_cast<char>(0xff), 'B', '\r', '\n'};
static std::string payload(bytes, sizeof(bytes));
static int readError = 0;

// Stub device access and selection; use the real FoE command implementation.
Command::Command(const std::string &n, const std::string &description):
    name(n), briefDesc(description), verbosity(Normal),
    emergency(false), force(false), json(false) {}
Command::~Command() {}
void Command::setOutputFile(const std::string &path) { outputFile = path; }
unsigned int Command::getSingleMasterIndex() const { return 0; }
Command::SlaveList Command::selectedSlaves(MasterDevice &)
{
    ec_ioctl_slave_t slave = {};
    return SlaveList(1, slave);
}
void Command::throwInvalidUsageException(const std::stringstream &s) const
{
    throw InvalidUsageException(s);
}
void Command::throwCommandException(const std::stringstream &s) const
{
    throw CommandException(s);
}
void Command::throwSingleSlaveRequired(unsigned int) const
{
    throw CommandException("Single slave required");
}
std::string Command::numericInfo() { return ""; }
MasterDevice::MasterDevice(unsigned int i):
    index(i), masterCount(1), fd(-1) {}
MasterDevice::~MasterDevice() {}
void MasterDevice::open(Permissions) {}
void MasterDevice::readFoe(ec_ioctl_slave_foe_t *data)
{
    if (readError) {
        data->result = readError == 1 ? FOE_TIMEOUT_ERROR : 0;
        throw MasterDeviceException("Simulated FoE read failure");
    }
    std::memcpy(data->buffer, payload.data(), payload.size());
    data->data_size = payload.size();
}

class FailedOutput: public std::streambuf
{
    protected:
        std::streamsize xsputn(const char *, std::streamsize) { return 0; }
        int_type overflow(int_type) { return traits_type::eof(); }
};

struct Result {
    std::string output, error;
};

static Result run(const std::string &path, bool failStdout = false)
{
    std::ostringstream capture;
    FailedOutput failure;
    std::streambuf *selected = capture.rdbuf();
    if (failStdout) selected = &failure;
    std::streambuf *previous = std::cout.rdbuf(selected);
    Result result;
    try {
        CommandFoeRead command;
        command.setOutputFile(path);
        command.execute(Command::StringVector(1, "remote.bin"));
    } catch (const std::exception &e) {
        result.error = e.what();
    }
    std::cout.rdbuf(previous);
    std::cout.clear();
    result.output = capture.str();
    return result;
}

static std::string contents(const std::string &path)
{
    std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
    std::ostringstream data;
    data << file.rdbuf();
    return data.str();
}

static unsigned int checks = 0, failures = 0;
static void check(bool ok, const char *description)
{
    checks++;
    if (!ok) {
        failures++;
        std::cerr << description << std::endl;
    }
}

int main()
{
    const char *tmp = getenv("TMPDIR");
    std::string pattern = std::string(tmp && *tmp ? tmp : "/tmp")
        + "/ethercat-foe-read-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char *directory = &buffer[0];
    if (!mkdtemp(directory)) {
        std::cerr << "mkdtemp(" << pattern << "): "
            << strerror(errno) << std::endl;
        return 1;
    }
    const std::string target = std::string(directory) + "/output.bin";
    const std::string absent = std::string(directory) + "/absent.bin";
    Result result = run("");
    check(result.error.empty() && result.output == payload,
            "Default stdout");
    result = run("-");
    check(result.error.empty() && result.output == payload,
            "Explicit stdout");
    result = run(target);
    check(result.error.empty() && result.output.empty()
            && contents(target) == payload, "Named binary output");
    { std::ofstream file(target.c_str()); file << "old and longer content"; }
    result = run(target);
    check(result.error.empty() && contents(target) == payload,
            "Replace existing file without trailing bytes");
    payload.clear();
    result = run(target);
    check(result.error.empty() && access(target.c_str(), F_OK) == 0
            && contents(target).empty(), "Empty file output");
    payload.assign(bytes, sizeof(bytes));
    result = run(std::string(directory) + "/missing/output.bin");
    check(result.error.find("Failed to open") != std::string::npos
            && result.output.empty(), "Open failure");
    { std::ofstream file(target.c_str()); file << "keep this backup"; }
    readError = 1;
    result = run(target);
    check(!result.error.empty() && result.output.empty()
            && contents(target) == "keep this backup",
            "Preserve on FoE error");
    result = run(absent);
    check(!result.error.empty() && access(absent.c_str(), F_OK) != 0,
            "Do not create file on FoE error");
    readError = 2;
    result = run(target);
    check(result.error == "Simulated FoE read failure"
            && contents(target) == "keep this backup", "Transport error");
    readError = 0;
    result = run("", true);
    check(result.error.find("Failed to write") != std::string::npos,
            "Standard output failure");
    if (access("/dev/full", W_OK) == 0) {
        result = run("/dev/full");
        check(result.error.find("Failed to write") != std::string::npos
                && result.output.empty(), "Buffered write/close failure");
    } else {
        std::cout << "Skipping /dev/full: device unavailable" << std::endl;
    }
    unlink(target.c_str());
    rmdir(directory);
    std::cout << checks << " checks, " << failures << " failures"
        << std::endl;
    return failures ? 1 : 0;
}
