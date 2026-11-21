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

// Master gflags for xCluster replication. The Rust master reads them by name, or accepts them
// without honoring them yet (see master_flags_baseline.txt). Their names, types, defaults, help
// text, tags and AutoFlag classes are part of the yb-master command line and of auto_flags.json:
// change them only as you would change any released flag.

#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"

DEFINE_test_flag(bool, xcluster_fail_table_create_during_bootstrap, false,
    "Fail the table or index creation during xcluster bootstrap stage.");

DEFINE_RUNTIME_int32(list_snapshot_backoff_increment_ms, 1000,
    "Number of milliseconds added to the delay between retries of fetching state of a snapshot. "
    "This delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(list_snapshot_backoff_increment_ms, stable);
TAG_FLAG(list_snapshot_backoff_increment_ms, advanced);

DEFINE_RUNTIME_int32(list_snapshot_max_backoff_sec, 10,
    "Maximum number of seconds to delay between retries of fetching state of a snpashot. This "
    "delay is applied after the RPC reties have been exhausted.");
TAG_FLAG(list_snapshot_max_backoff_sec, stable);
TAG_FLAG(list_snapshot_max_backoff_sec, advanced);

DEFINE_RUNTIME_uint32(xcluster_failover_restoration_wait_secs, 120,
    "Maximum number of seconds to wait for an xCluster failover restoration to finish before "
    "abandoning the wait and cleaning up the snapshot.");

DEFINE_RUNTIME_uint32(xcluster_failover_restoration_poll_interval_ms, 1000,
    "Delay, in milliseconds, between polls for the xCluster failover restoration state.");

DEFINE_RUNTIME_uint32(xcluster_failover_snapshot_delete_retry_secs, 30,
    "Number of seconds to keep retrying to delete an xCluster failover snapshot before giving up.");

DEFINE_RUNTIME_uint32(xcluster_failover_task_timeout_secs, 600,
    "Maximum number of seconds for the xCluster failover task to complete pre-restoration steps "
    "(pausing replication, creating and waiting for snapshots).");

DEFINE_RUNTIME_int32(xcluster_failover_snapshot_retention_hours, 1,
    "Retention time, in hours, for snapshots created during xCluster failover restore.");

DEFINE_test_flag(bool, pause_xcluster_failover_before_delete_replication, false,
    "If true, pause the xCluster failover task before deleting the replication group.");

DEFINE_test_flag(string, xcluster_failover_fail_restore_namespace_id, "",
    "If set, fail the xCluster failover restore when the given namespace id is being restored.");

DEFINE_test_flag(bool, xcluster_failover_fail_create_snapshot, false,
    "If set, fail the xCluster failover task at the create snapshot step.");

DEFINE_test_flag(bool, xcluster_failover_fail_delete_replication, false,
    "If set, simulate a failure when deleting the replication group during failover.");

// TODO: Bootstrap check sends multiple RPCs. Consider converting it to one RPC when enabling
// check_bootstrap_required flag by default.
DEFINE_RUNTIME_bool(check_bootstrap_required, false,
    "Is it necessary to check whether bootstrap is required for Universe Replication.");

DEFINE_RUNTIME_uint32(xcluster_ensure_sequence_updates_in_wal_timeout_sec, 3 * 60,
    "Timeout for XClusterEnsureSequenceUpdatesAreInWal RPCs.");
DEFINE_validator(xcluster_ensure_sequence_updates_in_wal_timeout_sec, FLAG_GT_VALUE_VALIDATOR(0));

DEFINE_test_flag(bool, allow_ycql_transactional_xcluster, false,
    "Determines if xCluster transactional replication on YCQL tables is allowed.");

DEFINE_test_flag(bool, fail_universe_replication_merge, false,
    "Causes MergeUniverseReplication to fail with an error.");

DEFINE_test_flag(bool, exit_unfinished_merging, false,
    "Whether to exit part way through the merging universe process.");

DEFINE_test_flag(int32, pause_at_start_of_setup_replication_group_ms, 0,
    "Delay in milliseconds before merging ALTER replication group into main replication group");

DEFINE_RUNTIME_AUTO_bool(enable_xcluster_api_v2, kExternal, false, true,
    "Allow the usage of v2 xCluster APIs that support DB Scoped replication groups");

DEFINE_RUNTIME_bool(disable_xcluster_db_scoped_new_table_processing, false,
    "When set disables automatic checkpointing of newly created tables on the source and adding "
    "table to inbound replication group on target");
TAG_FLAG(disable_xcluster_db_scoped_new_table_processing, advanced);

DEFINE_RUNTIME_AUTO_bool(enable_tablet_split_of_xcluster_replicated_tables, kExternal, false, true,
    "When set, it enables automatic tablet splitting for tables that are part of an "
    "xCluster replication setup");

DEFINE_RUNTIME_uint32(xcluster_ysql_statement_timeout_sec, 120,
    "Timeout for YSQL statements executed during xCluster operations.");

DEFINE_RUNTIME_AUTO_bool(xcluster_enable_ddl_replication, kExternal, false, true,
    "Enables xCluster automatic DDL replication.");

DEFINE_RUNTIME_AUTO_bool(xcluster_enable_target_applied_filter, kExternal, false, true,
    "When promoted, new xCluster streams in unidirectional automatic-DDL-mode replication "
    "groups use WritePB.xcluster_target_applied (instead of has_external_hybrid_time) as the "
    "producer's loop-prevention predicate, allowing index backfill writes to replicate "
    "end-to-end. Bi-directional and pre-existing streams are unaffected.");

DEFINE_RUNTIME_AUTO_bool(enable_xcluster_wal_anchor_stream_infra, kExternal, false, true,
    "Auto infra flag for the xCluster WAL_ANCHOR stream feature. NOTE: If you want to disable "
    "the feature, use enable_xcluster_wal_anchor_stream instead.");

DEFINE_RUNTIME_bool(enable_xcluster_wal_anchor_stream, true,
    "In xCluster automatic DDL replication mode, create a per table WAL_ANCHOR stream during the "
    "creation of a new table. The anchor pins the source WAL at the table's creation point so a "
    "target retry (after a DDL/transaction rollback) can still replay the full source WAL. Once "
    "the table is committed on the target, the anchor stream is deleted by a background task. When "
    "false, the source stops creating anchors for new tables, and the target stops asking for "
    "stream recreation on rollback and stops marking newly committed tables for anchor deletion. "
    "Anchors that are already marked are still deleted, so disable this on the source first, and "
    "on the target only once the source has no anchor streams left.");

DEFINE_test_flag(bool, force_automatic_ddl_replication_mode, false,
    "Make XClusterCreateOutboundReplicationGroup always use automatic instead of semi-automatic "
    "xCluster replication mode.");

DEFINE_test_flag(bool, return_legacy_universe_replication_info, false,
    "Omit fields that older masters did not populate in GetUniverseReplicationInfo responses.");

DEFINE_RUNTIME_AUTO_bool(ysql_auto_add_new_index_to_bidirectional_xcluster_infra, kExternal,
    false, true,
    "Determines if the system supports the capability of automatically adding ysql index to "
    "bi-directional xCluster. NOTE: Do not change this flag directly. If you want to disable the "
    "feature, use ysql_auto_add_new_index_to_bidirectional_xcluster instead.");

DEFINE_RUNTIME_bool(ysql_auto_add_new_index_to_bidirectional_xcluster, true,
    "If the indexed table is part of a bi-directional xCluster setup, then automatically add new "
    "indexes for this table to replication. This flag must be set on both universes, and the index "
    "must be created concurrently on both universes.");

DEFINE_RUNTIME_int32(xcluster_cleanup_tables_frequency_secs, /*half an hour*/ 30 * 60,
    "Frequency at which we will clean up old xCluster automatic mode"
    "DDL replication table entries.  0 means do not perform cleanup.");

DEFINE_RUNTIME_uint32(xcluster_ddl_tables_retention_secs, /*1 week*/ 7 * 24 * 60 * 60,
    "Age of DDL replication table entries beyond which they are cleaned up.  This needs to be "
    "at least one day.");
DEFINE_validator(xcluster_ddl_tables_retention_secs, FLAG_GE_VALUE_VALIDATOR(1 * 24 * 60 * 60));

DEFINE_RUNTIME_uint32(max_xcluster_streams_to_checkpoint_in_parallel, 200,
    "Maximum number of xCluster streams to checkpoint in parallel");

DEFINE_test_flag(bool, xcluster_fail_table_stream_checkpoint, false,
    "Fail the checkpoint of a table stream created for a target that is reconnecting a table.");

DEFINE_test_flag(bool, block_xcluster_checkpoint_namespace_task, false,
    "When enabled XClusterCheckpointNamespaceTask will be blocked");

DEFINE_RUNTIME_bool(xcluster_skip_health_check_on_replication_setup, false,
    "Skip health check on xCluster replication setup");

DEFINE_test_flag(bool, exit_unfinished_deleting, false,
    "Whether to exit part way through the deleting universe process.");

DEFINE_NON_RUNTIME_int32(xcluster_safe_time_table_num_tablets, 1,
    "Number of tablets to use when creating the xcluster safe time table. "
    "0 to use the same default num tablets as for regular tables.");
TAG_FLAG(xcluster_safe_time_table_num_tablets, advanced);

DEFINE_RUNTIME_uint32(xcluster_safe_time_log_outliers_interval_secs, 600,
    "Frequency in seconds at which to log outlier tablets for xcluster safe time.");

DEFINE_RUNTIME_uint32(xcluster_safe_time_slow_tablet_delta_secs, 600,
    "Lag in seconds at which a tablet is considered an outlier for xcluster safe time.");

DEFINE_RUNTIME_bool(enable_tablet_split_of_xcluster_bootstrapping_tables, false,
    "When set, it enables automatic tablet splitting for tables that are part of an "
    "xCluster replication setup and are currently being bootstrapped for xCluster.");

DEFINE_test_flag(bool, simulate_EnsureSequenceUpdatesAreInWal_failure, false,
    "Simulate failure during EnsureSequenceUpdatesAreInWal RPC.");

DEPRECATE_FLAG(bool, xcluster_wait_on_ddl_alter, "11_2024");

DEFINE_RUNTIME_uint32(add_new_index_to_bidirectional_xcluster_timeout_secs, 10 * 60,
    "Time in seconds within which index must be created on other universe when the indexed table "
    "is part of bidirectional xCluster replication. Applies only when "
    "--ysql_auto_add_new_index_to_bidirectional_xcluster is set.");

DEFINE_test_flag(bool, xcluster_pause_wal_anchor_deletion, false,
    "If set, skip send request for deleting the source WAL_ANCHOR streams.");

DEFINE_RUNTIME_uint32(xcluster_alter_universe_replication_setup_timeout_ms, 10 * 60 * 1000,
    "Maximum time in milliseconds to wait for setup replication to complete when altering "
    "universe replication.");

DEFINE_test_flag(bool, xcluster_wal_anchor_deletion_skip_marker_clear, false,
    "If set, send the WAL_ANCHOR delete RPC but skip clearing the pending deletion marker.");
