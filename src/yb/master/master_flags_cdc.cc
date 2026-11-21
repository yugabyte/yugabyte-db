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

// Master gflags for CDCSDK streams and replication slots. The Rust master reads them by name, or
// accepts them without honoring them yet (see master_flags_baseline.txt). Their names, types,
// defaults, help text, tags and AutoFlag classes are part of the yb-master command line and of
// auto_flags.json: change them only as you would change any released flag.

#include "yb/util/flags.h"

DEFINE_test_flag(bool, cdcsdk_skip_processing_dynamic_table_addition, false,
    "Skip finding unprocessed tables for cdcsdk streams");

DEFINE_test_flag(bool, cdcsdk_skip_processing_unqualified_tables, false,
    "Skip the bg task that finds and removes unprocessed unqualified tables from "
    "cdcsdk streams.");

DEFINE_test_flag(bool, disable_cdc_state_insert_on_setup, false,
    "Disable inserting new entries into cdc state as part of the setup flow.");

DEFINE_RUNTIME_int32(cdcsdk_table_processing_limit_per_run, 2,
    "The number of newly added tables we will add to CDCSDK streams, per run of the background "
    "task.");

DEFINE_RUNTIME_bool(xcluster_skip_schema_compatibility_checks_on_alter, false,
    "When xCluster replication sends a DDL change, skip checks "
    "for any schema compatibility");

DEFINE_RUNTIME_int32(wait_replication_drain_retry_timeout_ms, 2000,
    "Timeout in milliseconds in between CheckReplicationDrain calls to tservers "
    "in case of retries.");

DEFINE_RUNTIME_bool(disable_universe_gc, false, "Whether to run the GC on universes or not.");

DEFINE_RUNTIME_int32(cdc_parent_tablet_deletion_task_retry_secs, 30,
    "Frequency at which the background task will verify parent tablets retained for xCluster or "
    "CDCSDK replication and determine if they can be cleaned up.");

DEFINE_test_flag(bool, hang_wait_replication_drain, false,
    "Used in tests to temporarily block WaitForReplicationDrain.");

DEFINE_RUNTIME_AUTO_bool(cdc_enable_postgres_replica_identity, kLocalPersisted, false, true,
    "Enable new record types in CDC streams");

DEPRECATE_FLAG(bool, enable_backfilling_cdc_stream_with_replication_slot, "08_2026");

DEFINE_test_flag(bool, xcluster_fail_setup_stream_update, false, "Fail UpdateCDCStream RPC call");

DEFINE_RUNTIME_AUTO_bool(cdcsdk_enable_dynamic_tables_disable_option,
                        kLocalPersisted,
                        false,
                        true,
                        "This flag needs to be true in order to disable addition of dynamic tables "
                        "to CDC stream. This flag is required to be to true for execution of "
                        "yb-admin commands - "
                        "\'disable_dynamic_table_addition_on_change_data_stream\', "
                        "\'remove_user_table_from_change_data_stream\'");
TAG_FLAG(cdcsdk_enable_dynamic_tables_disable_option, advanced);
TAG_FLAG(cdcsdk_enable_dynamic_tables_disable_option, hidden);

DEFINE_test_flag(bool, cdcsdk_skip_updating_cdc_state_entries_on_table_removal, false,
    "Skip updating checkpoint to max for cdc state table entries while removing a user table from "
    "CDCSDK stream.");

DEFINE_test_flag(bool, cdcsdk_add_indexes_to_stream, false, "Allows addition of index to a stream");

DEPRECATE_FLAG(bool, cdcsdk_enable_cleanup_of_non_eligible_tables_from_stream, "09_2024");

DEFINE_test_flag(bool, cdcsdk_disable_drop_table_cleanup, false,
                 "When enabled, cleanup of dropped tables from CDC streams will be skipped.");

DEFINE_RUNTIME_AUTO_bool(cdcsdk_use_dropped_table_list_for_cleanup, kLocalPersisted, false, true,
    "When enabled, dropped tables are tracked via the dropped_table_id list in stream metadata "
    "instead of using the DELETING_METADATA stream state.");

DEFINE_RUNTIME_AUTO_bool(cdc_pg_create_grpc_stream, kLocalPersisted, false, true,
    "Allow creation of gRPC CDC streams via the YSQL replication slot interface using the yb_grpc "
    "plugin name. Also gates the post-upgrade backfill of legacy gRPC streams.");

DEFINE_test_flag(bool, cdcsdk_disable_deleted_stream_cleanup, false,
                 "When enabled, cleanup of deleted CDCSDK streams will be skipped.");

DEFINE_RUNTIME_AUTO_bool(cdcsdk_enable_identification_of_non_eligible_tables,
                         kLocalPersisted,
                         false,
                         true,
                         "This flag, when true, identifies all non-eligible tables that are part of"
                         " a CDC stream metadata while loading the CDC streams on a master "
                         "restart/leadership change. This identification happens on all CDC "
                         "streams in the universe");
TAG_FLAG(cdcsdk_enable_identification_of_non_eligible_tables, advanced);
TAG_FLAG(cdcsdk_enable_identification_of_non_eligible_tables, hidden);

DEFINE_test_flag(bool, cdcsdk_skip_table_removal_from_qualified_list, false,
                 "When enabled, table would not be removed from the qualified table list as part "
                 "of the table removal process from CDC stream");

DEFINE_RUNTIME_int32(cdc_create_stream_alter_table_dispatch_batch_size, 10,
    "Number of per-table AlterTable calls to dispatch in one batch during CDC stream creation. The "
    "value <= 0 signifies no batching.");

DEFINE_RUNTIME_int32(cdc_create_stream_alter_table_dispatch_delay_ms, 50,
    "If non-zero, sleeps for the configured number of milliseconds after each batch of "
    "cdc_create_stream_alter_table_dispatch_batch_size per-table AlterTable calls during CDC "
    "stream creation.");

DEFINE_RUNTIME_int32(max_concurrent_alter_table_rpcs, 0,
    "Maximum number of concurrent outstanding AsyncAlterTable RPCs the master will allow in "
    "flight. 0 disables the throttle entirely. Positive values are used as the absolute global "
    "cap. If its value is < 0 then the max_concurrent_alter_table_rpcs_per_tserver gflag and the "
    "number of live tservers are used to determine the limit. When the limit is reached, "
    "additional tasks block at the throttler and are rescheduled via the existing RetryingRpcTask "
    "backoff. It mirrors the semantics of max_concurrent_snapshot_rpcs.");

DEFINE_RUNTIME_int32(max_concurrent_alter_table_rpcs_per_tserver, 1,
    "Maximum number of concurrent outstanding AsyncAlterTable RPCs the master will allow in flight "
    "per tserver. Only used if the value of the gflag max_concurrent_alter_table_rpcs < 0. When "
    "used, it is multiplied by the live-tserver count to obtain the global cap on outstanding "
    "AsyncAlterTable RPCs. If the live-tserver count cannot be determined (e.g. cluster config not "
    "available during master startup) the per-tserver value is used as the global cap by itself. "
    "It mirrors max_concurrent_snapshot_rpcs_per_tserver.");

DEFINE_RUNTIME_bool(enable_truncate_cdcsdk_table, false,
    "When set, enables truncating tables currently part of a CDCSDK Stream");

DEFINE_RUNTIME_AUTO_bool(xcluster_store_older_schema_versions, kLocalPersisted, false, true,
    "When set, enables storing multiple older schema versions in xCluster replication stream "
    "metadata instead of just storing the current and previous schema versions.");

DEFINE_RUNTIME_uint32(xcluster_max_old_schema_versions, 50,
    "Maximum number of old schema versions to keep in xCluster replication stream metadata");

DEFINE_test_flag(bool, cdc_add_dynamic_index_to_state_table, false,
    "When enabled, adds the tablets of indexes created after slot creation to the cdc state table "
    "and sets retention barriers on their tablets.");
