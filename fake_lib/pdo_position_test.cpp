/*****************************************************************************
 *
 *  Copyright (C) 2026  StitchLilo626
 *  <43428443-StitchLilo626@users.noreply.gitlab.com>
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
 *  warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with the IgH EtherCAT master userspace library. If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 ****************************************************************************/

#include <ecrt.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>

static unsigned int checks = 0;
static unsigned int failures = 0;

#define EXPECT(EXPR) \
    do { \
        ++checks; \
        if (!(EXPR)) { \
            ++failures; \
            std::fprintf(stderr, "Line %d: %s\n", __LINE__, #EXPR); \
        } \
    } while (0)

struct Fixture
{
    ec_master_t *master;
    ec_domain_t *domain;
    ec_slave_config_t *sc;

    Fixture(): master(ecrt_request_master(0))
    {
        if (!master) {
            std::exit(EXIT_FAILURE);
        }
        domain = ecrt_master_create_domain(master);
        sc = ecrt_master_slave_config(master, 0, 0, 1, 2);
        if (!domain || !sc) {
            std::exit(EXIT_FAILURE);
        }
    }

    ~Fixture() { ecrt_release_master(master); }

    void configure(ec_pdo_info_t *pdos, unsigned int count)
    {
        ec_sync_info_t sync = {2, EC_DIR_OUTPUT, count, pdos, EC_WD_DEFAULT};
        EXPECT(ecrt_slave_config_pdos(sc, 1, &sync) == 0);
    }

    int position(unsigned int pdo, unsigned int entry,
                 unsigned int *bit = nullptr)
    {
        return ecrt_slave_config_reg_pdo_entry_pos(
                sc, 2, pdo, entry, domain, bit);
    }

    int index(uint16_t entry, unsigned int *bit = nullptr)
    {
        return ecrt_slave_config_reg_pdo_entry(sc, entry, 0, domain, bit);
    }
};

static void assignment_order_and_bounds()
{
    Fixture f;
    ec_pdo_entry_info_t first[] = {{0x6040, 0, 16}, {0x607a, 0, 16}};
    ec_pdo_entry_info_t second[] = {{0x6040, 0, 16}};
    ec_pdo_info_t pdos[] = {{0x1601, 2, first}, {0x1600, 1, second}};
    f.configure(pdos, 2);

    EXPECT(ecrt_slave_config_reg_pdo_entry_pos(
            f.sc, 3, 0, 0, f.domain, nullptr) < 0);
    EXPECT(f.position(2, 0) < 0);
    EXPECT(f.position(UINT_MAX, 0) < 0);
    EXPECT(f.position(0, 2) < 0);
    EXPECT(f.position(0, UINT_MAX) < 0);
    EXPECT(f.position(0x1601, 0) < 0);
    EXPECT(ecrt_domain_size(f.domain) == 0);

    // Register the later PDO first; layout still follows assignment order.
    EXPECT(f.position(1, 0) == 4);
    unsigned int bit = 99;
    EXPECT(f.position(0, 0, &bit) == 0);
    EXPECT(bit == 0);
    EXPECT(f.position(0, 1) == 2);
    EXPECT(f.index(0x6040) == 0);
    EXPECT(f.index(0x607a) == 2);
    EXPECT(f.position(1, 0) == 4);
    EXPECT(ecrt_domain_size(f.domain) == 6);
}

static void replace_configuration()
{
    Fixture f;
    ec_pdo_entry_info_t old_entry[] = {{0x6040, 0, 16}};
    ec_pdo_entry_info_t new_entry[] = {{0x607a, 0, 32}};
    ec_pdo_info_t old_pdos[] = {{0x1600, 1, old_entry}};
    ec_pdo_info_t new_pdos[] = {{0x1601, 1, new_entry}};
    f.configure(old_pdos, 1);
    f.configure(new_pdos, 1);
    f.configure(new_pdos, 1);

    EXPECT(f.index(0x6040) < 0);
    EXPECT(f.position(1, 0) < 0);
    EXPECT(f.position(0, 1) < 0);
    EXPECT(f.position(0, 0) == 0);
    EXPECT(ecrt_domain_size(f.domain) == 4);

    Fixture empty;
    empty.configure(old_pdos, 1);
    empty.configure(nullptr, 0);
    EXPECT(empty.position(0, 0) < 0);
    EXPECT(empty.index(0x6040) < 0);
    EXPECT(ecrt_domain_size(empty.domain) == 0);
}

static void incremental_configuration()
{
    Fixture f;
    EXPECT(ecrt_slave_config_sync_manager(
            f.sc, 2, EC_DIR_OUTPUT, EC_WD_DEFAULT) == 0);
    EXPECT(ecrt_slave_config_pdo_assign_add(f.sc, 2, 0x1600) == 0);
    EXPECT(ecrt_slave_config_pdo_assign_clear(f.sc, 2) == 0);
    EXPECT(ecrt_slave_config_pdo_assign_add(f.sc, 2, 0x1601) == 0);
    EXPECT(ecrt_slave_config_pdo_assign_add(f.sc, 2, 0x1600) == 0);
    EXPECT(ecrt_slave_config_pdo_mapping_add(
            f.sc, 0x1601, 0x6040, 0, 16) == 0);
    EXPECT(ecrt_slave_config_pdo_mapping_clear(f.sc, 0x1601) == 0);
    EXPECT(ecrt_slave_config_pdo_mapping_add(
            f.sc, 0x1601, 0x607a, 0, 32) == 0);
    EXPECT(ecrt_slave_config_pdo_mapping_add(
            f.sc, 0x1600, 0x6040, 0, 16) == 0);
    EXPECT(ecrt_slave_config_pdo_mapping_clear(f.sc, 0x1602) < 0);
    EXPECT(ecrt_slave_config_pdo_mapping_add(
            f.sc, 0x1602, 0x6040, 0, 16) < 0);
    EXPECT(f.position(1, 0) == 4);
    EXPECT(f.position(0, 0) == 0);
    EXPECT(f.index(0x6040) == 4);
    EXPECT(f.index(0x607a) == 0);
    EXPECT(ecrt_domain_size(f.domain) == 6);
}

static void failed_registration_and_retry(bool by_position, bool incremental)
{
    Fixture f;
    ec_pdo_entry_info_t bits[] = {{0, 0, 1}, {0x6000, 0, 7}};
    ec_pdo_entry_info_t aligned[] = {{0x607a, 0, 32}};
    ec_pdo_info_t old_pdos[] = {{0x1601, 2, bits}};
    ec_pdo_info_t new_pdos[] = {{0x1601, 1, aligned}};
    f.configure(old_pdos, 1);
    EXPECT((by_position ? f.position(0, 1) : f.index(0x6000)) < 0);
    EXPECT(ecrt_domain_size(f.domain) == 0);

    unsigned int bit = 99;
    ec_domain_t *bit_domain = ecrt_master_create_domain(f.master);
    if (!bit_domain) {
        std::exit(EXIT_FAILURE);
    }
    EXPECT((by_position ? ecrt_slave_config_reg_pdo_entry_pos(
            f.sc, 2, 0, 1, bit_domain, &bit)
            : ecrt_slave_config_reg_pdo_entry(
                    f.sc, 0x6000, 0, bit_domain, &bit)) == 0);
    EXPECT(bit == 1);
    EXPECT(ecrt_domain_size(bit_domain) == 1);

    // Reconfigure after rejection, before anything is mapped in f.domain.
    if (incremental) {
        EXPECT(ecrt_slave_config_pdo_assign_clear(f.sc, 2) == 0);
        EXPECT(ecrt_slave_config_pdo_assign_add(f.sc, 2, 0x1601) == 0);
        EXPECT(ecrt_slave_config_pdo_mapping_add(
                f.sc, 0x1601, 0x607a, 0, 32) == 0);
    }
    else {
        f.configure(new_pdos, 1);
    }
    EXPECT(f.index(0x6000) < 0);
    EXPECT(f.position(0, 1) < 0);
    EXPECT((by_position ? f.position(0, 0) : f.index(0x607a)) == 0);
    EXPECT(ecrt_domain_size(f.domain) == 4);
}

int main()
{
    // Isolate RtIPC from the user's settings, files and concurrent test runs.
    const char *tmp = getenv("TMPDIR");
    std::string pattern = std::string(tmp && *tmp ? tmp : "/tmp")
        + "/fake-pdo-position-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char *home = &buffer[0];
    if (!mkdtemp(home)) {
        std::fprintf(stderr, "mkdtemp(%s): %s\n", pattern.c_str(),
                std::strerror(errno));
        return EXIT_FAILURE;
    }
    if (setenv("FAKE_EC_HOMEDIR", home, 1)
        || setenv("FAKE_EC_NAME", "pdo-position", 1)) {
        rmdir(home);
        return EXIT_FAILURE;
    }
    assignment_order_and_bounds();
    replace_configuration();
    incremental_configuration();
    failed_registration_and_retry(false, false);
    failed_registration_and_retry(false, true);
    failed_registration_and_retry(true, false);
    failed_registration_and_retry(true, true);
    const std::string master_home = std::string(home) + "/0";
    EXPECT(rmdir(master_home.c_str()) == 0);
    EXPECT(rmdir(home) == 0);
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
