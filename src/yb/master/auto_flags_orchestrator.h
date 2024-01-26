// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include "yb/util/flags/auto_flags.h"
#include "yb/util/flags/auto_flags_util.h"
#include "yb/util/status.h"

namespace yb {
class AutoFlagsManager;
class CatalogManager;

namespace master {
// Create and persist a empty AutoFlags config with version set to 1.
// Intended to be used during the first process startup after the upgrade of clusters created on
// versions without AutoFlags.
Status CreateEmptyAutoFlagsConfig(AutoFlagsManager* auto_flag_manager);

// Create and persist a new AutoFlags config where all AutoFlags of class within
// FLAGS_limit_auto_flag_promote_for_new_universe are promoted and Apply it.
// Intended to be used in new cluster created with AutoFlags.
Status CreateAutoFlagsConfigForNewCluster(AutoFlagsManager* auto_flag_manager);

YB_DEFINE_ENUM(
    PromoteAutoFlagsOutcome, (kNoFlagsPromoted)(kNewFlagsPromoted)(kNonRuntimeFlagsPromoted));

// Promote eligible AutoFlags up to max_flag_class. If no new flags were eligible, Status
// AlreadyPresent is returned. When force is set, the config version is bumped up even if no new
// flags are eligible. Returns the new config version and whether any non-runtime flags were
// promoted.
Result<std::pair<uint32_t, PromoteAutoFlagsOutcome>> PromoteAutoFlags(
    const AutoFlagClass max_flag_class, const PromoteNonRuntimeAutoFlags promote_non_runtime_flags,
    const bool force, const AutoFlagsManager& auto_flag_manager, CatalogManager* catalog_manager);

Result<std::pair<uint32_t, PromoteAutoFlagsOutcome>> PromoteSingleAutoFlag(
    const ProcessName& process_name, const std::string& flag_name,
    const AutoFlagsManager& auto_flag_manager, CatalogManager* catalog_manager);

// Rollback AutoFlags to the specified version. Only Volatile AutoFlags are eligible for rollback.
// Returns weather any flags were rolled back and the new config version.
Result<std::pair<uint32_t, bool>> RollbackAutoFlags(
    uint32_t rollback_version, const AutoFlagsManager& auto_flag_manager,
    CatalogManager* catalog_manager);

// Demote a single AutoFlag. Returns weather the flag was demoted and the new config version.
// Note: This is extremely dangerous and should only be used under the guidance of YugabyteDB
// engineering team.
Result<std::pair<uint32_t, bool>> DemoteSingleAutoFlag(
    const ProcessName& process_name, const std::string& flag_name,
    const AutoFlagsManager& auto_flag_manager, CatalogManager* catalog_manager);
}  // namespace master

}  // namespace yb
