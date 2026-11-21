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

// Master gflags for tables, tablets, namespaces, index backfill, transaction tables and the catalog
// background tasks. The Rust master reads them by name, or accepts them without honoring them yet
// (see master_flags_baseline.txt). Their names, types, defaults, help text, tags and AutoFlag
// classes are part of the yb-master command line and of auto_flags.json: change them only as you
// would change any released flag.

#include "yb/common/common_flags.h"
#include "yb/master/master_defaults.h"
#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/size_literals.h"
#include "yb/util/tsan_util.h"

using namespace yb::size_literals;

static constexpr char kYbHnswUsearch[] = "yb_hnsw_usearch";
static constexpr char kYbHnswHnswlib[] = "yb_hnsw_hnswlib";

DEFINE_RUNTIME_int32(unresponsive_ts_rpc_timeout_ms, 15 * 60 * 1000,  // 15 minutes
             "After this amount of time (or after we have retried unresponsive_ts_rpc_retry_limit "
             "times, whichever happens first), the master will stop attempting to contact a tablet "
             "server or master in order to perform operations such as deleting a tablet.");
TAG_FLAG(unresponsive_ts_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_int32(unresponsive_ts_rpc_retry_limit, 20,
             "After this number of retries (or unresponsive_ts_rpc_timeout_ms expires, whichever "
             "happens first), the master will stop attempting to contact a tablet server or master "
             "in order to perform operations such as deleting a tablet.");
TAG_FLAG(unresponsive_ts_rpc_retry_limit, advanced);

DEFINE_RUNTIME_int32(retrying_ts_rpc_max_delay_ms, 60 * 1000,
             "Maximum delay between successive attempts to contact an unresponsive tablet server "
             "or master.");
TAG_FLAG(retrying_ts_rpc_max_delay_ms, advanced);

DEFINE_RUNTIME_int32(retrying_rpc_max_jitter_ms, 50,
    "Maximum random delay to add between rpc retry attempts.");
TAG_FLAG(retrying_rpc_max_jitter_ms, advanced);

DEFINE_test_flag(int32, slowdown_master_async_rpc_tasks_by_ms, 0,
                 "For testing purposes, slow down the run method to take longer.");

DEFINE_test_flag(bool, stuck_add_tablet_to_table_task_enabled, false, "description");

DEFINE_test_flag(string, skip_async_insert_packed_schema_for_tablet_id, "",
    "Tablet ID to skip AsyncInsertPackedSchemaForXClusterTarget requests for.");

DEFINE_RUNTIME_int32(ysql_index_backfill_rpc_timeout_ms, 5 * 60 * 1000, // 5 min.
    "Timeout used by the master when attempting to backfill a YSQL tablet during index creation.");
TAG_FLAG(ysql_index_backfill_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_int32(index_backfill_rpc_timeout_ms, 1 * 30 * 1000, // 30 sec.
    "Timeout used by the master when attempting to backfill a tablet during index creation.");
TAG_FLAG(index_backfill_rpc_timeout_ms, advanced);

DEFINE_RUNTIME_int32(index_backfill_rpc_max_retries, 150,
    "Number of times to retry backfilling a tablet chunk during index creation.");
TAG_FLAG(index_backfill_rpc_max_retries, advanced);

DEFINE_RUNTIME_int32(index_backfill_rpc_max_delay_ms, 10 * 60 * 1000, // 10 min.
    "Maximum delay before retrying a backfill tablet chunk request during index creation.");
TAG_FLAG(index_backfill_rpc_max_delay_ms, advanced);

DEFINE_RUNTIME_int32(index_backfill_wait_for_alter_table_completion_ms, 100,
    "Delay before retrying to see if an in-progress alter table has "
    "completed, during index backfill.");
TAG_FLAG(index_backfill_wait_for_alter_table_completion_ms, advanced);

DEFINE_RUNTIME_int32(index_backfill_tablet_split_completion_timeout_sec, 30,
    "Total time to wait for tablet splitting to complete on a table from which we are "
    "running a backfill before aborting the backfill and marking it as failed.");
TAG_FLAG(index_backfill_tablet_split_completion_timeout_sec, advanced);

DEFINE_RUNTIME_int32(index_backfill_tablet_split_completion_poll_freq_ms, 2000,
    "Delay before retrying to see if tablet splitting has completed on the table from "
    "which we are running a backfill.");
TAG_FLAG(index_backfill_tablet_split_completion_poll_freq_ms, advanced);

DEFINE_RUNTIME_bool(defer_index_backfill, false,
    "Defer index backfill so that backfills can be performed as a batch later on.");
TAG_FLAG(defer_index_backfill, advanced);

DEFINE_RUNTIME_bool(allow_batching_non_deferred_indexes, true,
    "If enabled, indexes on the same (YCQL) table may be batched together during "
    "backfill, even if they were not deferred.");
TAG_FLAG(allow_batching_non_deferred_indexes, advanced);

DEFINE_test_flag(bool, ysql_walk_removing_index_permissions, false,
    "Let CatalogManager::BackfillIndex advance a YSQL index through the removing permissions, "
    "the way the #28849 regression did, so tests can build the states it left behind.");

DEFINE_test_flag(int32, slowdown_backfill_alter_table_rpcs_ms, 0,
    "Slows down the send alter table rpc's so that the master may be stopped between "
    "different phases.");

DEFINE_test_flag(int32, slowdown_backfill_job_deletion_ms, 0,
    "Slows down backfill job deletion so that backfill job can be read by test.");

DEFINE_test_flag(bool, skip_index_backfill, false,
    "Skips backfilling the data on tservers and leaves the index in inconsistent state.");

DEFINE_test_flag(bool, block_do_backfill, false,
    "Block DoBackfill from proceeding.");

DEFINE_test_flag(bool, pause_compute_safe_time_for_backfill_read, false,
    "Pauses the compute safe time for backfill read.");

DEFINE_test_flag(bool, skip_ddl_requester_liveness_check, false,
    "Skip starting the requester liveness task. Used in tests to simulate the pre-fix behavior "
    "where master continues sending BackfillIndex RPCs after the backend is killed.");

DEFINE_test_flag(bool, simulate_empty_indexes_during_backfill, false,
    "Simulates BackfillTable::indexes_to_build() to return an empty set.");

DEFINE_test_flag(bool, simulate_cannot_enable_compactions, false,
    "Skips updating an index table to GC delete markers and sending of the corresponding RPC "
    "to the TServer.");

DEFINE_test_flag(int32, delay_clearing_fully_applied_ms, 0,
    "Amount of time to delay clearing the fully applied schema.");

DEFINE_RUNTIME_int32(catalog_manager_bg_task_wait_ms, 1000,
    "Amount of time the catalog manager background task thread waits between runs");

DEFINE_RUNTIME_int32(transaction_table_check_interval_sec, 15 * 60,
    "Interval in seconds for checking if the transaction status tables have expected tablets. "
    "Only used when autoscale_transaction_tables is true.");
TAG_FLAG(transaction_table_check_interval_sec, advanced);
DEFINE_validator(transaction_table_check_interval_sec, FLAG_GT_VALUE_VALIDATOR(0));

DEFINE_RUNTIME_bool(autoscale_transaction_tables, true,
    "Automatically scale transaction status tables based on the number of live tservers.");

DEFINE_RUNTIME_int32(load_balancer_initial_delay_secs, yb::master::kDelayAfterFailoverSecs,
             "Amount of time to wait between becoming master leader and enabling the load "
             "balancer.");

DEFINE_RUNTIME_bool(sys_catalog_respect_affinity_task, true,
            "Whether the master sys catalog tablet respects cluster config preferred zones "
            "and sends step down requests to a preferred leader.");

DEFINE_RUNTIME_int32(master_bg_task_long_operation_warning_ms, 5 * 1000,
    "Log warnings if the catalog manager background tasks (combined) take longer than this amount "
    "of time.");

DEFINE_test_flag(bool, pause_catalog_manager_bg_loop_start, false,
                 "Pause the bg tasks thread at the beginning of the loop.");

DEFINE_test_flag(bool, pause_catalog_manager_bg_loop_end, false,
                 "Pause the bg tasks thread at the end of the loop.");

// The time is temporarly set to 600 sec to avoid hitting the tablet replacement code inherited from
// Kudu. Removing tablet replacement code will be fixed in GH-6006
DEFINE_RUNTIME_int32(tablet_creation_timeout_ms, 600 * 1000,  // 600 sec
    "Timeout used by the master when attempting to create tablet "
    "replicas during table creation.");
TAG_FLAG(tablet_creation_timeout_ms, advanced);

DEFINE_test_flag(bool, disable_tablet_deletion, false,
    "Whether catalog manager should disable tablet deletion.");

DEFINE_test_flag(bool, get_ysql_catalog_version_from_sys_catalog, false,
    "Whether catalog manager should get the ysql catalog version "
    "from the sys_catalog.");

DEFINE_test_flag(uint32, abort_create_table, 0,
    "Fail the creation of a table whose name starts with test_create_abort_. 1 fails it before "
    "the table is written to the sys catalog, and the creation is undone. 2 fails the creation "
    "of an index after the index and its indexed table are written, and the creation is kept.");

// TODO: should this be a test flag?
DEFINE_RUNTIME_int32(catalog_manager_inject_latency_in_delete_table_ms, 0,
    "Number of milliseconds that the master will sleep in DeleteTable.");
TAG_FLAG(catalog_manager_inject_latency_in_delete_table_ms, hidden);

DEFINE_RUNTIME_int32(qos_max_db_count, 0,
    "Maximum number of YSQL databases allowed per cluster when QoS is enabled "
    "(enable_qos=true). CREATE DATABASE is rejected when the non-template database "
    "count would exceed this limit. Template databases (template0, template1) are "
    "excluded from the count. Set to 0 to disable the limit.");

DEFINE_RUNTIME_int32(replication_factor, 3,
    "Default number of replicas for tables that do not have the num_replicas set. "
    "Note: Changing this at runtime will only affect newly created tables.");
TAG_FLAG(replication_factor, advanced);

DEFINE_RUNTIME_int32(max_create_tablets_per_ts, 50,
    "The number of tablets per TS that can be requested for a new table.");
TAG_FLAG(max_create_tablets_per_ts, advanced);

// TODO: Is this code even useful?
DEFINE_RUNTIME_int32(master_failover_catchup_timeout_ms, 30 * 1000 * yb::kTimeMultiplier,  // 30 sec
    "Amount of time to give a newly-elected leader master to load"
    " the previous master's metadata and become active. If this time"
    " is exceeded, the node crashes.");
TAG_FLAG(master_failover_catchup_timeout_ms, advanced);
TAG_FLAG(master_failover_catchup_timeout_ms, experimental);

// TODO: should this be a test flag?
DEFINE_RUNTIME_bool(catalog_manager_check_ts_count_for_create_table, true,
    "Whether the master should ensure that there are enough live tablet "
    "servers to satisfy the provided replication count before allowing "
    "a table to be created.");
TAG_FLAG(catalog_manager_check_ts_count_for_create_table, hidden);

DEFINE_test_flag(bool, catalog_manager_check_yql_partitions_exist_for_is_create_table_done, true,
    "Whether the master should ensure that all of a table's tablets are "
    "in the YQL system.partitions vtable during the IsCreateTableDone check.");

DEFINE_test_flag(uint64, inject_latency_during_remote_bootstrap_secs, 0,
    "Number of seconds to sleep during a remote bootstrap.");

DEFINE_test_flag(bool, catalog_manager_simulate_system_table_create_failure, false,
    "This is only used in tests to simulate a failure where the table information is "
    "persisted in syscatalog, but the tablet information is not yet persisted and "
    "there is a failure.");

DEFINE_test_flag(bool, fail_table_creation_at_preparing_state, false,
    "This is only used in tests to simulate a failure that occurs when a table in "
    "process of creation is still in PREPARING state.");

DEFINE_test_flag(bool, fail_alter_table_after_commit, false,
    "If true, return an error after in-memory commit of the ALTER TABLE operation."
    "Used to force ALTER to fail at a deterministic spot.");

DEFINE_test_flag(bool, pause_before_send_hinted_election, false,
    "Inside StartElectionIfReady, pause before sending request for hinted election");

DEFINE_RUNTIME_int32(transaction_table_num_tablets, 0,
    "Number of tablets to use when creating the transaction status table."
    "0 to use transaction_table_num_tablets_per_tserver.");

DEFINE_RUNTIME_int32(transaction_table_num_tablets_per_tserver, kAutoDetectNumShardsPerTServer,
    "The default number of tablets per tablet server for transaction status table. If the value is "
    "-1, the system automatically determines an appropriate value based on number of CPU cores.");

DEFINE_RUNTIME_bool(auto_create_local_transaction_tables, true,
    "Whether or not to create local transaction status tables automatically on table "
    "creation with a tablespace with placement specified.");

DEFINE_test_flag(bool, name_transaction_tables_with_tablespace_id, false,
    "This is only used in tests to make associating automatically created transaction "
    "tables with their tablespaces easier, and causes transaction tables created "
    "automatically for tablespaces to include the tablespace oid in their names.");

DEFINE_test_flag(bool, consider_all_local_transaction_tables_local, false,
    "This is only used in tests, and forces the catalog manager to return all tablets "
    "of all transaction tables with placements as placement local, regardless of "
    "their placement.");

DEFINE_RUNTIME_bool(master_enable_metrics_snapshotter, false,
    "Should metrics snapshotter be enabled");

DEFINE_NON_RUNTIME_bool(master_enable_deleted_tablet_cleanup, true,
    "Whether the master drops DELETED tablets from its in-memory maps, including when loading "
    "the sys catalog, keeping only a split parent's children so a lookup for it still returns "
    "them. Sys catalog entries are untouched. Set to false to keep DELETED tablets in memory "
    "until their table is dropped.");

DEFINE_RUNTIME_int32(metrics_snapshots_table_num_tablets, 0,
    "Number of tablets to use when creating the metrics snapshots table."
    "0 to use the same default num tablets as for regular tables.");

DEFINE_RUNTIME_bool(disable_index_backfill, false,
    "A kill switch to disable multi-stage backfill for YCQL indexes.");
TAG_FLAG(disable_index_backfill, hidden);

DEFINE_RUNTIME_bool(disable_index_backfill_for_non_txn_tables, true,
    "A kill switch to disable multi-stage backfill for user enforced YCQL indexes. "
    "Note that enabling this feature may cause the create index flow to be slow. "
    "This is needed to ensure the safety of the index backfill process. See also "
    "index_backfill_upperbound_for_user_enforced_txn_duration_ms");
TAG_FLAG(disable_index_backfill_for_non_txn_tables, hidden);

DEFINE_RUNTIME_bool(enable_transactional_ddl_gc, true,
    "A kill switch for transactional DDL GC. Temporary safety measure.");
TAG_FLAG(enable_transactional_ddl_gc, hidden);

// TODO: should this be a test flag?
DEFINE_RUNTIME_bool(hide_pg_catalog_table_creation_logs, false,
    "Whether to hide detailed log messages for PostgreSQL catalog table creation. "
    "This cuts down test logs significantly.");
TAG_FLAG(hide_pg_catalog_table_creation_logs, hidden);

DEFINE_test_flag(int32, simulate_slow_table_create_secs, 0,
    "Simulates a slow table creation by sleeping after the table has been added to memory.");

DEFINE_test_flag(int32, simulate_slow_system_tablet_bootstrap_secs, 0,
    "Simulates a slow tablet bootstrap by adding a sleep before system tablet init.");

DEFINE_test_flag(bool, hang_on_namespace_transition, false,
    "Used in tests to simulate a lapse between issuing a namespace op and final processing.");

DEFINE_test_flag(bool, simulate_crash_after_table_marked_deleting, false,
    "Fail DeleteTable after the table and its indexes are written to the sys catalog as DELETING "
    "and before the deletion is applied in memory. This skips tablets deletion.");

DEPRECATE_FLAG(bool, master_drop_table_after_task_response, "11_2022");

DEFINE_test_flag(bool, tablegroup_master_only, false,
    "This is only for MasterTest to be able to test tablegroups without the"
    " transaction status table being created.");

DEFINE_RUNTIME_bool(enable_register_ts_from_raft, true,
    "Whether to register a tserver from the consensus information of a reported tablet.");

DEFINE_test_flag(bool, create_table_leader_hint_min_lexicographic, false,
    "Whether the Master should hint replica with smallest lexicographic rank for each "
    "tablet as leader initially on tablet creation.");

DEFINE_test_flag(int32, num_missing_tablets, 0, "Simulates missing tablets in a table");

DEFINE_RUNTIME_int32(partitions_vtable_cache_refresh_secs, 30,
    "Amount of time to wait before refreshing the system.partitions cached vtable. "
    "If generate_partitions_vtable_on_changes is true and this flag is > 0, then this background "
    "task will update the cached vtable using the internal map. "
    "If generate_partitions_vtable_on_changes is false and this flag is > 0, then this background "
    "task will be responsible for regenerating and updating the entire cached vtable.");

DEFINE_RUNTIME_bool(invalidate_yql_partitions_cache_on_create_table, true,
    "Whether the YCQL system.partitions vtable cache should be invalidated "
    "on a create table. Note that this requires "
    "partitions_vtable_cache_refresh_secs > 0 and "
    "generate_partitions_vtable_on_changes = false in order to take effect. "
    "If set to true, then this will ensure that newly created tables will be seen "
    "immediately in system.partitions.");

DEFINE_RUNTIME_int32(txn_table_wait_min_ts_count, 1,
    "Minimum Number of TS to wait for before creating the transaction status table."
    " Default value is 1. We wait for atleast --replication_factor if this value"
    " is smaller than that");
TAG_FLAG(txn_table_wait_min_ts_count, advanced);

DEFINE_RUNTIME_bool(enable_ysql_tablespaces_for_placement, true,
    "If set, tablespaces will be used for placement of YSQL tables.");

// Change the default value of this flag to false once we declare Colocation GA.
DEFINE_NON_RUNTIME_bool(ysql_legacy_colocated_database_creation, false,
    "Whether to create a legacy colocated database using pre-Colocation GA implementation");
TAG_FLAG(ysql_legacy_colocated_database_creation, advanced);

DEPRECATE_FLAG(int64, tablet_split_size_threshold_bytes, "10_2022");

DEFINE_RUNTIME_int64(tablet_split_low_phase_shard_count_per_node, 1,
    "The per-node tablet leader count until which a table is splitting at the phase 1 threshold, "
    "as defined by tablet_split_low_phase_size_threshold_bytes.");

DEFINE_RUNTIME_int64(tablet_split_high_phase_shard_count_per_node, 24,
    "The per-node tablet leader count until which a table is splitting at the phase 2 threshold, "
    "as defined by tablet_split_high_phase_size_threshold_bytes.");

DEFINE_RUNTIME_int64(tablet_split_low_phase_size_threshold_bytes, 128_MB,
    "The tablet size threshold at which to split tablets in phase 1. "
    "See tablet_split_low_phase_shard_count_per_node.");

DEFINE_RUNTIME_int64(tablet_split_high_phase_size_threshold_bytes, 10_GB,
    "The tablet size threshold at which to split tablets in phase 2. "
    "See tablet_split_high_phase_shard_count_per_node.");

DEFINE_RUNTIME_int64(tablet_force_split_threshold_bytes, 100_GB,
    "The tablet size threshold at which to split tablets regardless of how many tablets "
    "exist in the table already. This should be configured to prevent runaway whale "
    "tablets from forming in your cluster even if both automatic splitting phases have "
    "been finished.");

DEFINE_test_flag(bool, crash_server_on_sys_catalog_leader_affinity_move, false,
    "When set, crash the master process if it performs a sys catalog leader affinity "
    "move.");

DEFINE_test_flag(bool, validate_all_tablet_candidates, false,
    "When set to true, consider any tablet a valid candidate for splitting. "
    "Specifically this flag ensures that ValidateSplitCandidateTable and "
    "ValidateSplitCandidateTablet always return OK and all tablets are considered "
    "valid candidates for splitting.");

DEFINE_test_flag(bool, skip_placement_validation_createtable_api, false,
    "When set, it skips checking that all the tablets of a table have enough tservers"
    " conforming to the table placement policy during CreateTable API call.");

DEFINE_test_flag(int32, slowdown_alter_table_rpcs_ms, 0,
    "Slows down the alter table rpc's send and response handler so that the TServer "
    "has a heartbeat delay and triggers tablet leader change.");

DEFINE_test_flag(bool, reject_delete_not_serving_tablet_rpc, false,
    "Whether to reject DeleteNotServingTablet RPC.");

DEFINE_RUNTIME_bool(enable_delete_truncate_xcluster_replicated_table, false,
    "When set, enables deleting/truncating YCQL tables currently in xCluster replication. "
    "For YSQL tables, deletion is always allowed and TRUNCATE is always disallowed.");

DEFINE_test_flag(bool, sequential_colocation_ids, false,
    "When set, colocation IDs will be assigned sequentially (starting from 20001) "
    "rather than at random. This is especially useful for making pg_regress "
    "tests output consistent and predictable.");

DEFINE_test_flag(string, colocation_ids, "",
    "Comma separated list of colocation ids that should be used for the first created "
    "tables.");

DEFINE_RUNTIME_bool(disable_truncate_table, false,
    "When enabled, truncate table will be disallowed");

DEFINE_RUNTIME_bool(enable_truncate_on_pitr_table, false,
    "When enabled, truncate table will be allowed on PITR tables in YCQL. For PITR tables in YSQL, "
    "truncate is always allowed by default, and it can be turned off by setting the "
    "ysql_yb_enable_alter_table_rewrite autoflag to false.");

DEFINE_test_flag(double, fault_crash_after_registering_split_children, 0.0,
    "Crash after registering the children for a tablet split.");

DEFINE_test_flag(uint64, delay_sys_catalog_reload_secs, 0,
    "Number of seconds to sleep before a sys catalog reload.");

DEFINE_RUNTIME_bool(batch_ysql_system_tables_metadata, true,
    "Whether change metadata operation and SysCatalogTable upserts for ysql system tables during a "
    "create database is performed one by one or batched together");

DEFINE_test_flag(bool, keep_docdb_table_on_ysql_drop_table, false,
    "When enabled does not delete tables from the docdb layer, resulting in YSQL "
    "tables only being dropped in the postgres layer.");

DEFINE_RUNTIME_int32(max_concurrent_delete_replica_rpcs_per_ts, 50,
    "The maximum number of outstanding DeleteReplica RPCs sent to an individual tserver.");

DEFINE_RUNTIME_bool(enable_tablet_split_of_cdcsdk_streamed_tables, true,
    "When set, it enables automatic tablet splitting for tables that are part of a "
    "CDCSDK stream");

DEFINE_RUNTIME_bool(enable_tablet_split_of_replication_slot_streamed_tables, true,
    "When set, it enables automatic tablet splitting for tables that are part of replication "
    "slot's stream metadata");

DEFINE_test_flag(bool, duplicate_addtabletotablet_request, false,
    "Send a duplicate AddTableToTablet request to the tserver to simulate a retry.");

DEFINE_test_flag(bool, pause_before_upsert_ysql_sys_table, false,
                 "Pause before upserting a table in CreateYsqlSysTable.");

DEFINE_test_flag(bool, create_table_with_empty_pgschema_name, false,
    "Create YSQL tables with an empty pgschema_name field in their schema.");

DEFINE_test_flag(bool, create_table_with_empty_namespace_name, false,
    "Create YSQL tables with an empty namespace_name field in their schema.");

DEFINE_test_flag(int32, delay_split_registration_secs, 0,
    "Delay creating child tablets and upserting them to sys catalog");

DEFINE_test_flag(bool, pause_pg_catalog_versions_cache_refresh, false,
    "When true, RefreshPgCatalogVersionCache() returns without doing anything, for both its "
    "periodic caller and the on-demand caller at DDL commit "
    "(ysql_ddl_handler.cc). TEST_simulate_catalog_version_refresh_failure only stops the "
    "periodic task, which is not enough to make the cache fall behind: every DDL commit "
    "refreshes it on demand. Tests that need a cache genuinely behind pg_yb_catalog_version "
    "must use this flag.");

DEFINE_NON_RUNTIME_bool(enable_heartbeat_pg_catalog_versions_cache, true,
    "Whether to enable the use of heartbeat catalog versions cache for the "
    "pg_yb_catalog_version table which can help to reduce the number of reads "
    "from the table. This is more useful when there are many databases and/or "
    "many tservers in the cluster.");

DEFINE_test_flag(string, block_alter_table, "",
    "If non-empty, the specified alter table step is blocked. Possible values are "
    "\"alter_schema\" (blocks the schema from being altered), \"post_name_reservation\" (blocks "
    "a rename after its new name is reserved and before the table's COW write lock is taken), "
    "\"sys_catalog_write\" (blocks after the COW write lock is taken and before the sys-catalog "
    "write starts), and \"completion\" (blocks the service completion of the alter table "
    "request)");

DEFINE_RUNTIME_bool(master_join_existing_universe, false,
    "This flag helps prevent the accidental creation of a new universe. If the master_addresses "
    "flag is misconfigured or the on disk state of a master is wiped out the master could create a "
    "fresh universe, causing inconsistency with other masters in the universe and potential data "
    "loss. Setting this flag will prevent a master from creating a fresh universe regardless of "
    "other factors. To create a new universe with a new group of masters, unset this flag. Set "
    "this flag on all new and existing master processes once the universe creation completes.");

DEFINE_RUNTIME_uint32(initial_tserver_registration_duration_secs,
    yb::master::kDelayAfterFailoverSecs,
    "Amount of time to wait between becoming master leader and relying on all live TServers having "
    "registered.");
TAG_FLAG(initial_tserver_registration_duration_secs, advanced);

DEFINE_NON_RUNTIME_bool(emergency_repair_mode, false,
    "Starts yb-master in emergency repair mode where CatalogManager is not started.");
TAG_FLAG(emergency_repair_mode, advanced);
TAG_FLAG(emergency_repair_mode, unsafe);

DEPRECATE_FLAG(bool, vector_index_use_hnswlib, "02_2026");

DEPRECATE_FLAG(bool, vector_index_use_yb_hnsw, "02_2026");

DEFINE_RUNTIME_string(vector_index_backend, kYbHnswHnswlib,
    "Which vector index backend to use. Options are \"yb_hnsw_usearch\" and \"yb_hnsw_hnswlib\".");
DEFINE_validator(vector_index_backend,
    FLAG_IN_SET_VALIDATOR(kYbHnswUsearch, kYbHnswHnswlib));
TAG_FLAG(vector_index_backend, hidden);
TAG_FLAG(vector_index_backend, advanced);

DEFINE_RUNTIME_bool(vector_index_store_payload, false,
    "Whether newly created tables and vector indexes replace the vector reverse mapping "
    "with a payload attached to every vector in the index chunks. The payload carries the ybctid, "
    "so search resolves rows without the reverse mapping, and such a table writes no reverse "
    "mapping entries at all, neither on insert nor on delete. The value is fixed for a table and "
    "for an index when it is created, and indexes of a table that writes no reverse mapping always "
    "store the payload. Disabled by default, because deleted vectors are not removed from such an "
    "index yet, see #33912.");

DEFINE_RUNTIME_AUTO_bool(enable_table_owned_vector_reverse_mapping, kExternal, false, true,
    "When true, newly created YSQL tables hold vector reverse mapping ownership. "
    "Such tables write vector reverse mappings on row insert/update regardless of "
    "whether a vector index exists, and vector index backfill skips reverse mapping.");

DEFINE_test_flag(int32, table_owned_vector_reverse_mapping, -1,
    "When > -1, override owns_vector_reverse_mapping during YSQL table creation.");

DEFINE_test_flag(int32, system_table_num_tablets, -1,
    "Number of tablets to use when creating the system tables. "
    "If -1, the number of tablets will follow the value provided in the CreateTable request.");

DEFINE_RUNTIME_AUTO_bool(enable_tablespace_based_transaction_placement, kLocalPersisted,
    false, true,
    "Enable support for tablespace-based transaction locality.");

DEFINE_test_flag(bool, fail_yugabyte_namespace_creation_on_second_attempt, false,
    "Fail CopyPgsqlSysTables for yugabyte database on the second creation attempt "
    "to simulate failure during pg_restore phase of YSQL major upgrade");

DEFINE_test_flag(bool, enable_multi_way_tablet_split, false,
    "Enable splitting a tablet into more than two child tablets");

DEFINE_test_flag(bool, cdcsdk_disable_stream_drop_during_db_drop, false,
    "When enabled, the DeleteNamespace workflow won't mark associated CDCSDK streams as DELETING.");

DEFINE_RUNTIME_AUTO_bool(ycql_allow_cassandra_drop, kLocalPersisted, false, true,
    "When true, stops the regeneration of the cassandra user on restart of the cluster.");

DEFINE_RUNTIME_bool(enforce_tablet_replica_limits, false,
    "When set, create table and split tablet operations will be blocked if they would cause the "
    "total number of tablet replicas running in the universe to be too large. The limit for the "
    "maximum number of tablet replicas the universe can support is set by the flags "
    "tablet_replicas_per_gib_limit and tablet_replicas_per_core_limit.");
TAG_FLAG(enforce_tablet_replica_limits, advanced);

DEFINE_RUNTIME_bool(use_cache_for_partitions_vtable, true,
            "Whether we should use caching for system.partitions table.");

DEFINE_RUNTIME_bool(generate_partitions_vtable_on_changes, false,
            "Whether we should generate the system.partitions vtable whenever relevant partition "
            "changes occur.");

DEFINE_test_flag(uint64, slow_cluster_config_load_secs, 0,
                 "When set, it pauses load of cluster config during sys catalog load.");
