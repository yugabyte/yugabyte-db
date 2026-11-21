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

// Master gflags for snapshots, PITR, import/export and clone. The Rust master reads them by name,
// or accepts them without honoring them yet (see master_flags_baseline.txt). Their names, types,
// defaults, help text, tags and AutoFlag classes are part of the yb-master command line and of
// auto_flags.json: change them only as you would change any released flag.

#include "yb/util/flags.h"

DEFINE_test_flag(bool, simulate_long_restore, false,
    "Simulate a long restore failing to transition task to completion state if successful, thereby "
    "constantly retrying the RESTORE_ON_TABLET operation.");

DEFINE_test_flag(bool, pause_issuing_tserver_snapshot_requests, false,
                 "Whether to halt before issuing snapshot operations to tservers.");

DEPRECATE_FLAG(bool, enable_transaction_snapshots, "08_2024");

DEPRECATE_FLAG(bool, allow_consecutive_restore, "10_2022");

DEFINE_RUNTIME_bool(enable_namespace_snapshot_workflow, true,
    "Enable namespace-based snapshot creation based on namespace_id. Can be disabled to fallback "
    "to the old snapshot creation workflow where client provides the set of tables to collect as "
    "part of snapshot");

DEFINE_test_flag(bool, import_snapshot_failed, false,
    "Return a error from ImportSnapshotMeta RPC for testing the RPC failure.");

DEFINE_test_flag(bool, skip_oid_advance_on_restore, false,
    "Skip advancing OID counters on restore if true.");

DEFINE_RUNTIME_uint64(import_snapshot_max_concurrent_create_table_requests, 20,
    "Maximum number of create table requests to the master that can be outstanding "
    "during the import snapshot metadata phase of restore.");

DEFINE_RUNTIME_bool(enable_fast_pitr, true,
    "Whether fast restore of sys catalog on the master is enabled.");

DEFINE_RUNTIME_uint32(default_snapshot_retention_hours, 24,
    "Number of hours for which to keep the snapshot around. Only used if no value was provided "
    "by the client when creating the snapshot.");

DEFINE_RUNTIME_bool(import_snapshot_using_table_name, false,
    "Use the old workflow of import snapshot where table names in backup/restore sides are used to "
    "build the mappings between tables in backup and restore side. This flag can be enabled as a "
    "safety button in case restore using relfilenode fails.");

DEFINE_RUNTIME_AUTO_bool(enable_export_snapshot_using_relfilenode, kExternal, false, true,
    "Enable exporting snapshots with the new format version = 3 that uses relfilenodes.");

DEFINE_RUNTIME_AUTO_bool(enable_db_clone, kLocalPersisted, false, true, "Enable DB cloning.");
TAG_FLAG(enable_db_clone, advanced);

DEFINE_test_flag(bool, fail_clone_pg_schema, false, "Fail clone pg schema operation for testing");

DEFINE_test_flag(bool, fail_clone_tablets, false, "Fail ImportSnapshotAndStartTabletsCloning for "
    "testing");

DEFINE_test_flag(bool, pause_before_enabling_db_connections, false,
    "If set, pause the clone workflow right before re-enabling connections to the target "
    "database (i.e. while the target's pg_database.datallowconn is still false).");

DEFINE_test_flag(uint32, delay_after_clearing_tserver_metacache_ms, 0,
    "Sleep this long in the ClearMetaCache callback between recording that a tserver responded "
    "and acting on it. Lines the per-tserver callbacks up so that any non-atomic 'am I the last "
    "one' check schedules EnableDbConnections more than once.");

