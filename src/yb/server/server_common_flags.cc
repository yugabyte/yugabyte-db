// Copyright (c) YugabyteDB, Inc.
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

// This file contains gflags used by the yb-master and yb-tserver processes.  In general, they are
// flags we do not expect to be needed by tools.

#include "yb/server/server_common_flags.h"

#include "yb/util/flags.h"
#include "yb/util/flag_validators.h"

// User specified identifier for this cluster. On the first master leader setup, this is stored in
// the cluster_config. if not specified, a random UUID is generated.
// Changing the value after setup is not recommended.
DEFINE_NON_RUNTIME_string(cluster_uuid, "", "Cluster UUID to be used by this cluster");
TAG_FLAG(cluster_uuid, hidden);

// NOTE: This flag guards proto changes and it is not safe to enable during an upgrade, or rollback
// once enabled. If you want to change the default to true then you will have to make it a
// kLocalPersisted AutoFlag.
DEFINE_NON_RUNTIME_bool(enable_pg_cron, false,
    "Enables the pg_cron extension. Jobs will be run on a single tserver node. The node should be "
    "assumed to be selected randomly.");

DEFINE_RUNTIME_AUTO_PG_FLAG(
    bool, yb_allow_replication_slot_lsn_types, kLocalPersisted, false, true,
    "Enable LSN types to be specified while creating replication slots.");

DEFINE_RUNTIME_PG_PREVIEW_FLAG(
    bool, yb_allow_replication_slot_ordering_modes, false,
    "Enable ordering modes to be specified while creating replication slots.");

// This autoflag was introduced in commit 80def06f8c19ad7cbc52f41b4be48d158157a418, but it had some
// flaws and the feature required a regular gFlag. As of 27.11.2024 there does not exist an infra to
// remove or demote an autoflag, therefore this flag is going to be a dummy flag. A new gFlag
// ycql_ignore_group_by_error in introduced for the same functionality.
DEFINE_RUNTIME_AUTO_bool(ycql_suppress_group_by_error, kLocalVolatile, true, false,
    "This flag is deprecated, please use ycql_ignore_group_by_error");

DEFINE_RUNTIME_AUTO_bool(ysql_yb_enable_advisory_locks, kLocalPersisted, false, true,
    "Whether to enable advisory locks.");

DEFINE_RUNTIME_PG_FLAG(int32, yb_major_version_upgrade_compatibility, 0,
    "The compatibility level to use during a YSQL Major version upgrade. Allowed values are 0 and "
    "11.");
DEFINE_validator(ysql_yb_major_version_upgrade_compatibility, FLAG_IN_SET_VALIDATOR(0, 11));

// DevNote: If this flag is changed to runtime then it needs to be converted to RUNTIME_PG_FLAG,
// and pggate/webserver/ybc_pg_webserver_wrapper.cc needs to be updated to use the guc instead of
// the gFlag, since gFlags in PG are not updated at runtime.
// Connection manager doesn't support yet changing gflag at runtime.
DEFINE_NON_RUNTIME_uint32(ysql_conn_mgr_max_client_connections, 10000,
    "Total number of concurrent client connections that the Ysql Connection Manager allows.");
DEFINE_validator(ysql_conn_mgr_max_client_connections, FLAG_GT_VALUE_VALIDATOR(1));

DEFINE_RUNTIME_AUTO_PG_FLAG(bool, yb_upgrade_to_pg15_completed, kLocalPersisted, false, true,
    "Indicates the state of YSQL major upgrade to PostgreSQL version 15. Do not modify this "
    "manually.");

namespace yb {

bool IsYsqlMajorVersionUpgradeInProgress() {
  // yb_upgrade_to_pg15_completed is only available on the newer code version.
  // So we use yb_major_version_upgrade_compatibility to determine if the YSQL major upgrade is in
  // progress on processes running the older version.
  // We cannot rely on yb_major_version_upgrade_compatibility only, since it will be reset in the
  // Monitoring Phase.
  //  DevNote: Keep this in sync with YBCPgYsqlMajorVersionUpgradeInProgress.
  return FLAGS_ysql_yb_major_version_upgrade_compatibility > 0 ||
         !FLAGS_ysql_yb_upgrade_to_pg15_completed;
}

}  // namespace yb
