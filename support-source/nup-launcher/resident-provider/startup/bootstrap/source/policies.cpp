// SPDX-License-Identifier: GPL-2.0-only
#include "policies.hpp"
#include "../../common/policy_package.hpp"

namespace {
constexpr u64 MigrationProgramId = 0x040000000000003a;
constexpr char MigrationMain[] = "/kaihen/bin/hen/main";
constexpr char MigrationPackage[] = "/kaihen/bin/hen/exefs.nsp";
}

Result prepareMigrationPackage(const BootstrapInput& input, BootstrapOperation* stage) {
    SignedPolicy policy = {};
    *stage = BootstrapOperation_ReadMigrationPolicy;
    Result rc = readSignedPolicy(input.loader_root_session, MigrationProgramId,
                                 NcmStorageId_BuiltInSystem, 1, &policy);
    if (R_FAILED(rc)) return rc;

    *stage = BootstrapOperation_WriteMigrationPackage;
    return writeExeFsPackage(input.fs_root_session, MigrationMain, MigrationPackage, policy);
}
