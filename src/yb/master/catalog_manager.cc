// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
// ================================================================================================
//
// The catalog manager handles the current list of tables
// and tablets in the cluster, as well as their current locations.
// Since most operations in the master go through these data
// structures, locking is carefully managed here to prevent unnecessary
// contention and deadlocks:
//
// - each structure has an internal spinlock used for operations that
//   are purely in-memory (eg the current status of replicas)
// - data that is persisted on disk is stored in separate PersistentTable(t)Info
//   structs. These are managed using copy-on-write so that writers may block
//   writing them back to disk while not impacting concurrent readers.
//
// Usage rules:
// - You may obtain READ locks in any order. READ locks should never block,
//   since they only conflict with COMMIT which is a purely in-memory operation.
//   Thus they are deadlock-free.
// - If you need a WRITE lock on both a table and one or more of its tablets,
//   acquire the lock on the table first. This strict ordering prevents deadlocks.
//
// ================================================================================================

#include "yb/master/catalog_manager.h"

#include <stdlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/optional.hpp>

#include "yb/cdc/cdc_state_table.h"

#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/universe_key_client.h"

#include "yb/common/colocated_util.h"
#include "yb/common/common.pb.h"
#include "yb/common/common_flags.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/common_util.h"
#include "yb/common/constants.h"
#include "yb/common/entity_ids.h"
#include "yb/common/key_encoder.h"
#include "yb/common/pg_catversions.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_type_util.h"
#include "yb/common/roles_permissions.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/quorum_util.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/partial_row.h"
#include "yb/dockv/partition.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"
#include "yb/gutil/walltime.h"

#include "yb/master/async_rpc_tasks.h"
#include "yb/master/backfill_index.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_entity_parser.h"
#include "yb/master/catalog_loaders.h"
#include "yb/master/catalog_manager-internal.h"
#include "yb/master/catalog_manager_bg_tasks.h"
#include "yb/master/catalog_manager_util.h"
#include "yb/master/clone/clone_state_manager.h"
#include "yb/master/cluster_balance.h"
#include "yb/master/encryption_manager.h"
#include "yb/master/leader_epoch.h"
#include "yb/master/master.h"
#include "yb/master/master_admin.pb.h"
#include "yb/master/master_client.pb.h"
#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_dcl.pb.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/master_encryption.pb.h"
#include "yb/master/master_error.h"
#include "yb/master/master_fwd.h"
#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/master_replication.pb.h"
#include "yb/master/master_snapshot_coordinator.h"
#include "yb/master/master_util.h"
#include "yb/master/object_lock_info_manager.h"
#include "yb/master/permissions_manager.h"
#include "yb/master/post_tablet_create_task_base.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog_constants.h"
#include "yb/master/tablet_split_manager.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/xcluster/xcluster_manager.h"
#include "yb/master/yql_aggregates_vtable.h"
#include "yb/master/yql_auth_resource_role_permissions_index.h"
#include "yb/master/yql_auth_role_permissions_vtable.h"
#include "yb/master/yql_auth_roles_vtable.h"
#include "yb/master/yql_columns_vtable.h"
#include "yb/master/yql_functions_vtable.h"
#include "yb/master/yql_indexes_vtable.h"
#include "yb/master/yql_keyspaces_vtable.h"
#include "yb/master/yql_local_vtable.h"
#include "yb/master/yql_partitions_vtable.h"
#include "yb/master/yql_peers_vtable.h"
#include "yb/master/yql_size_estimates_vtable.h"
#include "yb/master/yql_tables_vtable.h"
#include "yb/master/yql_triggers_vtable.h"
#include "yb/master/yql_types_vtable.h"
#include "yb/master/yql_views_vtable.h"
#include "yb/master/ysql/ysql_manager.h"
#include "yb/master/ysql_ddl_verification_task.h"
#include "yb/master/ysql_tablegroup_manager.h"
#include "yb/master/ysql/ysql_initdb_major_upgrade_handler.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_retention_policy.h"

#include "yb/tserver/remote_bootstrap_client.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_error.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/atomic.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/oid_generator.h"
#include "yb/util/random_util.h"
#include "yb/util/rw_mutex.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/status.h"
#include "yb/util/stopwatch.h"
#include "yb/util/string_case.h"
#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/threadpool.h"
#include "yb/util/to_stream.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/uuid.h"
#include "yb/util/yb_pg_errcodes.h"

#include "yb/yql/redis/redisserver/redis_constants.h"

using namespace std::literals;
using namespace yb::size_literals;

// TODO: Cannot be runtime state due to cdc_client...
DEFINE_NON_RUNTIME_int32(master_ts_rpc_timeout_ms, 30 * 1000,  // 30 sec
    "Timeout used for the Master->TS async rpc calls.");
TAG_FLAG(master_ts_rpc_timeout_ms, advanced);

// The time is temporarly set to 600 sec to avoid hitting the tablet replacement code inherited from
// Kudu. Removing tablet replacement code will be fixed in GH-6006
DEFINE_RUNTIME_int32(
    tablet_creation_timeout_ms, 600 * 1000,  // 600 sec
    "Timeout used by the master when attempting to create tablet "
    "replicas during table creation.");
TAG_FLAG(tablet_creation_timeout_ms, advanced);

DEFINE_test_flag(bool, disable_tablet_deletion, false,
                 "Whether catalog manager should disable tablet deletion.");

DEFINE_test_flag(bool, get_ysql_catalog_version_from_sys_catalog, false,
                 "Whether catalog manager should get the ysql catalog version "
                 "from the sys_catalog.");


// TODO: should this be a test flag?
DEFINE_RUNTIME_int32(catalog_manager_inject_latency_in_delete_table_ms, 0,
    "Number of milliseconds that the master will sleep in DeleteTable.");
TAG_FLAG(catalog_manager_inject_latency_in_delete_table_ms, hidden);

DECLARE_int32(catalog_manager_bg_task_wait_ms);

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

DECLARE_bool(master_ignore_deleted_on_load);

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

DEFINE_test_flag(bool, pause_before_send_hinted_election, false,
                 "Inside StartElectionIfReady, pause before sending request for hinted election");

DECLARE_string(cluster_uuid);

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

DEFINE_test_flag(bool, return_error_if_namespace_not_found, false,
    "Return an error from ListTables if a namespace id is not found in the map");

DEFINE_test_flag(bool, hang_on_namespace_transition, false,
    "Used in tests to simulate a lapse between issuing a namespace op and final processing.");

DEFINE_test_flag(bool, simulate_crash_after_table_marked_deleting, false,
    "Crash yb-master after table's state is set to DELETING. This skips tablets deletion.");

DEPRECATE_FLAG(bool, master_drop_table_after_task_response, "11_2022");

DEFINE_test_flag(bool, tablegroup_master_only, false,
                 "This is only for MasterTest to be able to test tablegroups without the"
                 " transaction status table being created.");

DEFINE_RUNTIME_bool(enable_register_ts_from_raft, true,
    "Whether to register a tserver from the consensus information of a reported tablet.");

DECLARE_int32(tserver_unresponsive_timeout_ms);

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

DEFINE_NON_RUNTIME_int32(ysql_tablespace_info_refresh_secs, 30,
    "Frequency at which the table to tablespace information will be updated in master "
    "from pg catalog tables. A value of -1 disables the refresh task.");

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

DEFINE_test_flag(double, crash_after_registering_split_tablets, 0.0,
    "Crash inside CatalogManager::RegisterNewTabletsForSplit after calling Upsert.");

DEFINE_test_flag(bool, error_after_registering_split_tablets, false,
    "Return an error inside CatalogManager::RegisterNewTabletsForSplit "
    "after calling Upsert.");

DEFINE_RUNTIME_bool(enable_delete_truncate_xcluster_replicated_table, false,
            "When set, enables deleting/truncating YCQL tables currently in xCluster replication. "
            "For YSQL tables, deletion is always allowed and TRUNCATE is always disallowed.");

DEFINE_test_flag(bool, sequential_colocation_ids, false,
                 "When set, colocation IDs will be assigned sequentially (starting from 20001) "
                 "rather than at random. This is especially useful for making pg_regress "
                 "tests output consistent and predictable.");

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

DECLARE_bool(transaction_tables_use_preferred_zones);

DECLARE_string(tmp_dir);

DEFINE_RUNTIME_bool(batch_ysql_system_tables_metadata, true,
    "Whether change metadata operation and SysCatalogTable upserts for ysql system tables during a "
    "create database is performed one by one or batched together");

DEFINE_test_flag(bool, keep_docdb_table_on_ysql_drop_table, false,
                 "When enabled does not delete tables from the docdb layer, resulting in YSQL "
                 "tables only being dropped in the postgres layer.");

DEFINE_RUNTIME_int32(max_concurrent_delete_replica_rpcs_per_ts, 50,
    "The maximum number of outstanding DeleteReplica RPCs sent to an individual tserver.");

DEFINE_RUNTIME_bool(
    enable_truncate_cdcsdk_table, false,
    "When set, enables truncating tables currently part of a CDCSDK Stream");

DEFINE_RUNTIME_bool(enable_tablet_split_of_cdcsdk_streamed_tables, true,
    "When set, it enables automatic tablet splitting for tables that are part of a "
    "CDCSDK stream");

DEFINE_RUNTIME_bool(enable_tablet_split_of_replication_slot_streamed_tables, false,
    "When set, it enables automatic tablet splitting for tables that are part of replication "
    "slot's stream metadata");

METRIC_DEFINE_gauge_uint32(cluster, num_tablet_servers_live,
                           "Number of live tservers in the cluster", yb::MetricUnit::kUnits,
                           "The number of tablet servers that have responded or done a heartbeat "
                           "in the time interval defined by the gflag "
                           "FLAGS_tserver_unresponsive_timeout_ms.");

METRIC_DEFINE_gauge_uint32(cluster, num_tablet_servers_dead,
                           "Number of dead tservers in the cluster", yb::MetricUnit::kUnits,
                           "The number of tablet servers that have not responded or done a "
                           "heartbeat in the time interval defined by the gflag "
                           "FLAGS_tserver_unresponsive_timeout_ms.");

METRIC_DEFINE_gauge_uint64(cluster, max_follower_heartbeat_delay, "The maximum heartbeat delay "
                           "among all master followers",
                           yb::MetricUnit::kMilliseconds,
                           "The number of milliseconds since the master leader has ACK'd a "
                           "heartbeat from all peers of the system catalog tablet. The master "
                           "leader does not ACK heartbeats from peers that have fallen behind due "
                           "to WAL GC so such peers will cause this metric to increase "
                           "indefinitely.");

METRIC_DEFINE_counter(cluster, create_table_too_many_tablets,
    "How many CreateTable requests have failed due to too many tablets", yb::MetricUnit::kRequests,
    "The number of CreateTable request errors due to attempting to create too many tablets.");

DEFINE_test_flag(bool, duplicate_addtabletotablet_request, false,
                 "Send a duplicate AddTableToTablet request to the tserver to simulate a retry.");

DEFINE_test_flag(bool, create_table_in_running_state, false,
    "In master-only tests, create tables in the running state without waiting for tablet creation, "
    "as we will not have any tablet servers.");

DEFINE_test_flag(bool, pause_before_upsert_ysql_sys_table, false,
                 "Pause before upserting a table in CreateYsqlSysTable.");

DEFINE_test_flag(bool, create_table_with_empty_pgschema_name, false,
    "Create YSQL tables with an empty pgschema_name field in their schema.");

DEFINE_test_flag(bool, create_table_with_empty_namespace_name, false,
    "Create YSQL tables with an empty namespace_name field in their schema.");

DEFINE_test_flag(int32, delay_split_registration_secs, 0,
                 "Delay creating child tablets and upserting them to sys catalog");

DECLARE_bool(ysql_enable_colocated_tables_with_tablespaces);

DEFINE_NON_RUNTIME_bool(enable_heartbeat_pg_catalog_versions_cache, false,
    "Whether to enable the use of heartbeat catalog versions cache for the "
    "pg_yb_catalog_version table which can help to reduce the number of reads "
    "from the table. This is more useful when there are many databases and/or "
    "many tservers in the cluster.");

DEFINE_test_flag(string, block_alter_table, "",
    "If non-empty, the specified alter table step is blocked. Possible values are "
    "\"alter_schema\" (blocks the schema from being altered) and \"completion\","
    "(blocks the service completion of the alter table request)");

DECLARE_bool(master_enable_universe_uuid_heartbeat_check);

DECLARE_int32(heartbeat_interval_ms);

DEFINE_RUNTIME_bool(master_join_existing_universe, false,
    "This flag helps prevent the accidental creation of a new universe. If the master_addresses "
    "flag is misconfigured or the on disk state of a master is wiped out the master could create a "
    "fresh universe, causing inconsistency with other masters in the universe and potential data "
    "loss. Setting this flag will prevent a master from creating a fresh universe regardless of "
    "other factors. To create a new universe with a new group of masters, unset this flag. Set "
    "this flag on all new and existing master processes once the universe creation completes.");

DEFINE_test_flag(bool, disable_set_catalog_version_table_in_perdb_mode, false,
                 "Whether to disable setting the catalog version table in perdb mode.");
DEFINE_RUNTIME_uint32(initial_tserver_registration_duration_secs,
    yb::master::kDelayAfterFailoverSecs,
    "Amount of time to wait between becoming master leader and relying on all live TServers having "
    "registered.");
TAG_FLAG(initial_tserver_registration_duration_secs, advanced);

DEFINE_NON_RUNTIME_bool(emergency_repair_mode, false,
    "Starts yb-master in emergency repair mode where CatalogManager is not started.");
TAG_FLAG(emergency_repair_mode, advanced);
TAG_FLAG(emergency_repair_mode, unsafe);

DECLARE_bool(ysql_yb_enable_replica_identity);

DECLARE_bool(enable_pg_cron);

DECLARE_bool(TEST_enable_object_locking_for_table_locks);

namespace yb {
namespace master {

using std::shared_ptr;
using std::string;
using std::unique_ptr;
using std::vector;
using std::set;
using std::min;
using std::map;
using std::pair;

using namespace std::placeholders;

using consensus::kMinimumTerm;
using consensus::CONSENSUS_CONFIG_COMMITTED;
using consensus::CONSENSUS_CONFIG_ACTIVE;
using consensus::Consensus;
using consensus::ConsensusStatePB;
using consensus::GetConsensusRole;
using consensus::PeerMemberType;
using consensus::RaftPeerPB;
using consensus::StartRemoteBootstrapRequestPB;
using dockv::Partition;
using dockv::PartitionSchema;
using rpc::RpcContext;
using server::MonitoredTask;
using strings::Substitute;
using tablet::TABLET_DATA_DELETED;
using tablet::TABLET_DATA_TOMBSTONED;
using tablet::TabletDataState;
using tablet::RaftGroupMetadataPtr;
using tablet::TabletPeer;
using tablet::RaftGroupStatePB;
using yb::server::MasterAddressesToString;

using yb::client::YBSchema;
using yb::client::YBSchemaBuilder;

// Macros to access index information in CATALOG.
//
// NOTES from file master.proto for SysTablesEntryPB.
// - For index table: [to be deprecated and replaced by "index_info"]
//     optional bytes indexed_table_id = 13; // Indexed table id of this index.
//     optional bool is_local_index = 14 [ default = false ];  // Whether this is a local index.
//     optional bool is_unique_index = 15 [ default = false ]; // Whether this is a unique index.
// - During transition period, we have to consider both fields and the following macros help
//   avoiding duplicate protobuf version check thru out our code.

const std::string& GetIndexedTableId(const SysTablesEntryPB& pb) {
  return pb.has_index_info() ? pb.index_info().indexed_table_id() : pb.indexed_table_id();
}

namespace {

#define PROTO_GET_IS_LOCAL(tabpb) \
  (tabpb.has_index_info() ? tabpb.index_info().is_local() \
                          : tabpb.is_local_index())

#define PROTO_GET_IS_UNIQUE(tabpb) \
  (tabpb.has_index_info() ? tabpb.index_info().is_unique() \
                          : tabpb.is_unique_index())


#define PROTO_PTR_IS_INDEX(tabpb) \
  (tabpb->has_index_info() || !tabpb->indexed_table_id().empty())

#define PROTO_PTR_IS_TABLE(tabpb) \
  (!tabpb->has_index_info() && tabpb->indexed_table_id().empty())

#if (0)
// Once the deprecated fields are obsolete, the above macros should be defined as the following.
#define GetIndexedTableId(tabpb) (tabpb.index_info().indexed_table_id())
#define PROTO_GET_IS_LOCAL(tabpb) (tabpb.index_info().is_local())
#define PROTO_GET_IS_UNIQUE(tabpb) (tabpb.index_info().is_unique())
#define PROTO_IS_INDEX(tabpb) (tabpb.has_index_info())
#define PROTO_IS_TABLE(tabpb) (!tabpb.has_index_info())
#define PROTO_PTR_IS_INDEX(tabpb) (tabpb->has_index_info())
#define PROTO_PTR_IS_TABLE(tabpb) (!tabpb->has_index_info())

#endif

class IndexInfoBuilder {
 public:
  explicit IndexInfoBuilder(IndexInfoPB* index_info) : index_info_(*index_info) {
    DVLOG(3) << " After " << __PRETTY_FUNCTION__ << " index_info_ is " << yb::ToString(index_info_);
  }

  void ApplyProperties(const TableId& indexed_table_id, bool is_local, bool is_unique) {
    index_info_.set_indexed_table_id(indexed_table_id);
    index_info_.set_version(0);
    index_info_.set_is_local(is_local);
    index_info_.set_is_unique(is_unique);
    DVLOG(3) << " After " << __PRETTY_FUNCTION__ << " index_info_ is " << yb::ToString(index_info_);
  }

  Status ApplyColumnMapping(const Schema& indexed_schema, const Schema& index_schema) {
    for (size_t i = 0; i < index_schema.num_columns(); i++) {
      const auto& col_name = index_schema.column(i).name();
      const auto indexed_col_idx = indexed_schema.find_column(col_name);
      if (PREDICT_FALSE(indexed_col_idx == Schema::kColumnNotFound)) {
        return STATUS(NotFound, "The indexed table column does not exist", col_name);
      }
      auto* col = index_info_.add_columns();
      col->set_column_id(index_schema.column_id(i));
      col->set_indexed_column_id(indexed_schema.column_id(indexed_col_idx));
    }
    index_info_.set_hash_column_count(narrow_cast<uint32_t>(index_schema.num_hash_key_columns()));
    index_info_.set_range_column_count(narrow_cast<uint32_t>(index_schema.num_range_key_columns()));

    for (size_t i = 0; i < indexed_schema.num_hash_key_columns(); i++) {
      index_info_.add_indexed_hash_column_ids(indexed_schema.column_id(i));
    }
    for (size_t i = indexed_schema.num_hash_key_columns(); i < indexed_schema.num_key_columns();
        i++) {
      index_info_.add_indexed_range_column_ids(indexed_schema.column_id(i));
    }
    DVLOG(3) << " After " << __PRETTY_FUNCTION__ << " index_info_ is " << yb::ToString(index_info_);
    return Status::OK();
  }

 private:
  IndexInfoPB& index_info_;
};

MasterErrorPB_Code NamespaceMasterError(SysNamespaceEntryPB_State state) {
  switch (state) {
    case SysNamespaceEntryPB::PREPARING: FALLTHROUGH_INTENDED;
    case SysNamespaceEntryPB::DELETING:
      return MasterErrorPB::IN_TRANSITION_CAN_RETRY;
    case SysNamespaceEntryPB::DELETED: FALLTHROUGH_INTENDED;
    case SysNamespaceEntryPB::FAILED: FALLTHROUGH_INTENDED;
    case SysNamespaceEntryPB::RUNNING:
      return MasterErrorPB::INTERNAL_ERROR;
    default:
      FATAL_INVALID_ENUM_VALUE(SysNamespaceEntryPB_State, state);
  }
}

size_t GetNameMapperIndex(YQLDatabase db_type) {
  switch (db_type) {
    case YQL_DATABASE_UNKNOWN: break;
    case YQL_DATABASE_CQL: return 1;
    case YQL_DATABASE_PGSQL: return 2;
    case YQL_DATABASE_REDIS: return 3;
  }
  CHECK(false) << "Unexpected db type " << db_type;
  return 0;
}

bool IsIndexBackfillEnabled(TableType table_type, bool is_transactional) {
  // Fetch the runtime flag to prevent any issues from the updates to flag while processing.
  const bool disabled =
      (table_type == PGSQL_TABLE_TYPE
          ? GetAtomicFlag(&FLAGS_ysql_disable_index_backfill)
          : GetAtomicFlag(&FLAGS_disable_index_backfill) ||
      (!is_transactional && GetAtomicFlag(&FLAGS_disable_index_backfill_for_non_txn_tables)));
  return !disabled;
}

constexpr auto kDefaultYQLPartitionsRefreshBgTaskSleep = 10s;

int GetTransactionTableNumShardsPerTServer() {
  int value = 8;
  if (IsTsan()) {
    value = 2;
  } else if (base::NumCPUs() <= 2) {
    value = 4;
  }
  return value;
}

void InitMasterFlags() {
  yb::InitCommonFlags();
  if (GetAtomicFlag(&FLAGS_transaction_table_num_tablets_per_tserver) ==
      kAutoDetectNumShardsPerTServer) {
    const auto value = GetTransactionTableNumShardsPerTServer();
    VLOG(1) << "Auto setting FLAGS_transaction_table_num_tablets_per_tserver to " << value;
    CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(transaction_table_num_tablets_per_tserver, value));
  }
}

Result<bool> DoesTableExist(const Result<TableInfoPtr>& result) {
  if (result.ok()) {
    return true;
  }
  if (result.status().IsNotFound()
      && MasterError(result.status()) == MasterErrorPB::OBJECT_NOT_FOUND) {
    return false;
  }
  return result.status();
}

// Orders all servers in the masters argument by their score in ascending order. Scores are inverse
// priorities, i.e. masters with lower scores have a greater priority to act as the sys catalog
// tablet leader. Scores are computed from the affinitized_zones and the blacklist. All masters with
// a score greater than or equal to the current leader are dropped from the list. Also returns a
// bool which is true if the current leader is a valid choice for the sys catalog tablet leader.
//
// If a master is blacklisted, it has the highest possible score.
// Otherwise if a master is not in any affinitized zone, it has the second highest score.
// Otherwise the score of a master is the index of its placement zone in the affinitized_zones
// input.
const std::pair<std::vector<std::pair<ServerEntryPB, size_t>>, bool>
GetMoreEligibleSysCatalogLeaders(
    const vector<AffinitizedZonesSet>& affinitized_zones, const BlacklistSet& blacklist,
    const std::vector<ServerEntryPB>& masters, const ServerRegistrationPB& current_leader) {
  std::unordered_map<CloudInfoPB, size_t, cloud_hash, cloud_equal_to> cloud_info_scores;
  for (size_t i = 0; i < affinitized_zones.size(); ++i) {
    for (const auto& cloud_info : affinitized_zones[i]) {
      cloud_info_scores.insert({cloud_info, i});
    }
  }
  auto get_score = [&](const ServerRegistrationPB& registration) {
    if (IsBlacklisted(registration, blacklist)) {
      return affinitized_zones.size() + 1;
    } else {
      auto cloud_score = cloud_info_scores.find(registration.cloud_info());
      if (cloud_score == cloud_info_scores.end()) {
        return affinitized_zones.size();
      } else {
        return cloud_score->second;
      }
    }
  };

  auto my_score = get_score(current_leader);
  std::vector<std::pair<ServerEntryPB, size_t>> scored_masters;
  for (const auto& master : masters) {
    auto master_score = get_score(master.registration());
    if (master_score < my_score) {
      scored_masters.push_back({master, get_score(master.registration())});
    }
  }

  std::sort(scored_masters.begin(), scored_masters.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.second < rhs.second;
  });
  return {scored_masters, my_score == 0 || my_score < affinitized_zones.size()};
}

// Sets basic fields in the TabletLocationsPB proto that are always filled regardless of the
// PartitionsOnly parameter.
void InitializeTabletLocationsPB(
    const TabletId& tablet_id, const SysTabletsEntryPB& pb, TabletLocationsPB* locs_pb) {
  locs_pb->set_table_id(pb.table_id());
  locs_pb->set_tablet_id(tablet_id);
  locs_pb->mutable_partition()->CopyFrom(pb.partition());
  locs_pb->set_split_depth(pb.split_depth());
  locs_pb->set_split_parent_tablet_id(pb.split_parent_tablet_id());
}

IndexStatusPB::BackfillStatus GetBackfillStatus(IndexPermissions permissions) {
  switch (permissions) {
    case INDEX_PERM_READ_WRITE_AND_DELETE:
      return IndexStatusPB::BACKFILL_SUCCESS;
    case INDEX_PERM_DELETE_ONLY:                     [[fallthrough]];
    case INDEX_PERM_WRITE_AND_DELETE:                [[fallthrough]];
    case INDEX_PERM_DO_BACKFILL:                     [[fallthrough]];
    case INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING: [[fallthrough]];
    case INDEX_PERM_DELETE_ONLY_WHILE_REMOVING:      [[fallthrough]];
    case INDEX_PERM_INDEX_UNUSED:                    [[fallthrough]];
    case INDEX_PERM_NOT_USED:
      return IndexStatusPB::BACKFILL_UNKNOWN;
  }
  FATAL_INVALID_ENUM_VALUE(IndexPermissions, permissions);
}

IndexStatusPB::BackfillStatus GetBackfillStatus(const IndexInfoPB& index) {
  // It is expected index permissions are always specified.
  return index.has_index_permissions() ? GetBackfillStatus(index.index_permissions())
                                       : IndexStatusPB::BACKFILL_UNKNOWN;
}

bool IsPgCronJobTable(const CreateTableRequestPB& req) {
  return req.has_schema() && req.schema().has_pgschema_name() &&
         req.schema().pgschema_name() == "cron" && req.name() == "job";
}

Result<QLWriteRequestPB::QLStmtType> ToQLStmtType(
    WriteSysCatalogEntryRequestPB::WriteOp pb_op_type) {
  switch (pb_op_type) {
    case WriteSysCatalogEntryRequestPB::SYS_CATALOG_INSERT:
      return QLWriteRequestPB::QL_STMT_INSERT;
    case WriteSysCatalogEntryRequestPB::SYS_CATALOG_UPDATE:
      return QLWriteRequestPB::QL_STMT_UPDATE;
    case WriteSysCatalogEntryRequestPB::SYS_CATALOG_DELETE:
      return QLWriteRequestPB::QL_STMT_DELETE;
  }

  return STATUS_FORMAT(
      InvalidArgument, "Unsupported type $0",
      WriteSysCatalogEntryRequestPB::WriteOp_Name(pb_op_type));
}

}  // anonymous namespace

////////////////////////////////////////////////////////////
// CatalogManager
////////////////////////////////////////////////////////////

CatalogManager::NamespaceInfoMap& CatalogManager::NamespaceNameMapper::operator[](
    YQLDatabase db_type) {
  return typed_maps_[GetNameMapperIndex(db_type)];
}

const CatalogManager::NamespaceInfoMap& CatalogManager::NamespaceNameMapper::operator[](
    YQLDatabase db_type) const {
  return typed_maps_[GetNameMapperIndex(db_type)];
}

void CatalogManager::NamespaceNameMapper::clear() {
  for (auto& m : typed_maps_) {
    m.clear();
  }
}

std::vector<scoped_refptr<NamespaceInfo>> CatalogManager::NamespaceNameMapper::GetAll() const {
  std::vector<scoped_refptr<NamespaceInfo>> result;
  for (const auto& map : typed_maps_) {
    for (const auto& [_, ns_info] : map) {
      if (ns_info) {
        result.emplace_back(ns_info);
      }
    }
  }

  return result;
}

CatalogManager::CatalogManager(Master* master, SysCatalogTable* sys_catalog)
    : master_(DCHECK_NOTNULL(master)),
      sys_catalog_(DCHECK_NOTNULL(sys_catalog)),
      tablet_exists_(false),
      state_(kConstructed),
      leader_ready_term_(-1),
      leader_lock_(RWMutex::Priority::PREFER_WRITING),
      load_balance_policy_(std::make_unique<ClusterLoadBalancer>(this)),
      tablegroup_manager_(std::make_unique<YsqlTablegroupManager>()),
      object_lock_info_manager_(std::make_unique<ObjectLockInfoManager>(master_, this)),
      permissions_manager_(std::make_unique<PermissionsManager>(this)),
      tasks_tracker_(new TasksTracker(IsUserInitiated::kFalse)),
      jobs_tracker_(new TasksTracker(IsUserInitiated::kTrue)),
      encryption_manager_(new EncryptionManager()),
      tablespace_manager_(std::make_shared<YsqlTablespaceManager>(nullptr, nullptr)),
      tablespace_bg_task_running_(false) {
  InitMasterFlags();
  CHECK_OK(ThreadPoolBuilder("leader-initialization")
               .set_max_threads(1)
               .Build(&leader_initialization_pool_));
  CHECK_OK(ThreadPoolBuilder("CatalogManagerBGTasks").Build(&background_tasks_thread_pool_));
  CHECK_OK(ThreadPoolBuilder("async-tasks").Build(&async_task_pool_));
  CHECK_OK(sys_catalog_->Start(Bind(&CatalogManager::ElectedAsLeaderCb, Unretained(this))));
  xcluster_manager_ = std::make_unique<XClusterManager>(*master_, *this, *sys_catalog_);
  ysql_manager_ = std::make_unique<YsqlManager>(*master_, *this, *sys_catalog_);
}

CatalogManager::~CatalogManager() {
  if (StartShutdown()) {
    CompleteShutdown();
  }
}

Status CatalogManager::Init() {
  {
    std::lock_guard l(state_lock_);
    CHECK_EQ(kConstructed, state_);
    state_ = kStarting;
  }

  // Initialize the metrics emitted by the catalog manager.
  load_balance_policy_->InitMetrics();

  metric_num_tablet_servers_live_ =
    METRIC_num_tablet_servers_live.Instantiate(master_->metric_entity_cluster(), 0);

  metric_num_tablet_servers_dead_ =
    METRIC_num_tablet_servers_dead.Instantiate(master_->metric_entity_cluster(), 0);

  metric_create_table_too_many_tablets_ =
      METRIC_create_table_too_many_tablets.Instantiate(master_->metric_entity_cluster());

  metric_max_follower_heartbeat_delay_ =
    METRIC_max_follower_heartbeat_delay.Instantiate(master_->metric_entity_cluster(), 0);

  cdc_state_table_ = std::make_unique<cdc::CDCStateTable>(master_->cdc_state_client_future());

  RETURN_NOT_OK(xcluster_manager_->Init());

  RETURN_NOT_OK_PREPEND(InitSysCatalogAsync(),
                        "Failed to initialize sys tables async");

  if (PREDICT_FALSE(FLAGS_TEST_simulate_slow_system_tablet_bootstrap_secs > 0)) {
    LOG_WITH_PREFIX(INFO) << "Simulating slow system tablet bootstrap";
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_simulate_slow_system_tablet_bootstrap_secs));
  }

  // WaitUntilRunning() must run outside of the lock as to prevent
  // deadlock. This is safe as WaitUntilRunning waits for another
  // thread to finish its work and doesn't itself depend on any state
  // within CatalogManager. Need not start sys catalog or background tasks
  // when we are started in shell mode.
  if (!master_->opts().IsShellMode()) {
    RETURN_NOT_OK(GetUniverseKeyRegistryFromOtherMastersAsync());
    RETURN_NOT_OK(EnableBgTasks());
  }

  // Cache the server registration even for shell mode masters. See
  // https://github.com/yugabyte/yugabyte-db/issues/8065.
  RETURN_NOT_OK(GetRegistration(&server_registration_));

  {
    std::lock_guard l(state_lock_);
    CHECK_EQ(kStarting, state_);
    state_ = kRunning;
  }

  Started();

  return Status::OK();
}

XClusterManagerIf* CatalogManager::GetXClusterManager() {
  return xcluster_manager_.get();
}

YsqlManagerIf& CatalogManager::GetYsqlManager() { return GetYsqlManagerImpl(); }

Status CatalogManager::ElectedAsLeaderCb() {
  if (FLAGS_emergency_repair_mode) {
    // In this mode the sys_catalog leader will not start CatalogManager. The only operations that
    // can be performed in this mode are DumpSysCatalogEntries and WriteSysCatalogEntry.
    return Status::OK();
  }

  time_elected_leader_.store(MonoTime::Now());
  return leader_initialization_pool_->SubmitClosure(
      Bind(&CatalogManager::LoadSysCatalogDataTask, Unretained(this)));
}

Status CatalogManager::WaitUntilCaughtUpAsLeader(const MonoDelta& timeout) {
  string uuid = master_->fs_manager()->uuid();
  auto tablet = VERIFY_RESULT(tablet_peer()->shared_tablet_safe());
  auto consensus = VERIFY_RESULT(tablet_peer()->GetConsensus());
  ConsensusStatePB cstate = consensus->ConsensusState(CONSENSUS_CONFIG_ACTIVE);
  if (!cstate.has_leader_uuid() || cstate.leader_uuid() != uuid) {
    return STATUS_SUBSTITUTE(IllegalState,
        "Node $0 not leader. Consensus state: $1", uuid, cstate.ShortDebugString());
  }

  // Wait for all transactions to be committed.
  const CoarseTimePoint deadline = CoarseMonoClock::now() + timeout;
  {
    tablet::HistoryCutoffPropagationDisabler disabler(tablet->RetentionPolicy());
    RETURN_NOT_OK(tablet_peer()->operation_tracker()->WaitForAllToFinish(timeout));
  }

  RETURN_NOT_OK(consensus->WaitForLeaderLeaseImprecise(deadline));
  return Status::OK();
}

void CatalogManager::LoadSysCatalogDataTask() {
  if (FLAGS_TEST_delay_sys_catalog_reload_secs > 0) {
    LOG(INFO) << "Sleeping for " << FLAGS_TEST_delay_sys_catalog_reload_secs
              << " secs due to fault injection by test";
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_delay_sys_catalog_reload_secs));
  }
  auto consensus_result = tablet_peer()->GetConsensus();
  if (!consensus_result) {
    // This could happen for e.g. during a shutdown.
    LOG_WITH_PREFIX(WARNING) << "Could not get consensus for sys catalog tablet: "
                             << consensus_result.status();
    return;
  }
  auto& consensus = consensus_result.get();

  const int64_t term = consensus->ConsensusState(CONSENSUS_CONFIG_ACTIVE).current_term();
  Status s = WaitUntilCaughtUpAsLeader(
      MonoDelta::FromMilliseconds(FLAGS_master_failover_catchup_timeout_ms));

  int64_t term_after_wait = consensus->ConsensusState(CONSENSUS_CONFIG_ACTIVE).current_term();
  if (term_after_wait != term) {
    // If we got elected leader again while waiting to catch up then we will get another callback to
    // update state from sys_catalog, so bail now.
    //
    // If we failed when waiting, i.e. could not acquire a leader lease, this could be due to us
    // becoming a follower. If we're not partitioned away, we'll know about a new term soon.
    LOG_WITH_PREFIX(INFO)
        << "Term change from " << term << " to " << term_after_wait
        << " while waiting for master leader catchup. Not loading sys catalog metadata. "
        << "Status of waiting: " << s;
    return;
  }

  if (!s.ok()) {
    // This could happen e.g. if we are a partitioned-away leader that failed to acquire a leader
    // lease.
    //
    // TODO: handle this cleanly by transitioning to a follower without crashing.
    LOG_WITH_PREFIX(WARNING) << "Failed waiting for node to catch up after master election: " << s;

    if (s.IsTimedOut()) {
      LOG_WITH_PREFIX(FATAL) << "Shutting down due to unavailability of other masters after"
                             << " election. TODO: Abdicate instead.";
    }
    return;
  }

  LOG_WITH_PREFIX(INFO) << "Loading table and tablet metadata into memory for term " << term;
  SysCatalogLoadingState state{ LeaderEpoch(term, pitr_count()) };

  LOG_SLOW_EXECUTION(WARNING, 1000, LogPrefix() + "Loading metadata into memory") {
    Status status = VisitSysCatalog(&state);
    if (!status.ok()) {
      {
        std::lock_guard l(state_lock_);
        if (state_ == kClosing) {
          LOG_WITH_PREFIX(INFO)
              << "Error loading sys catalog; because shutdown is in progress. term " << term
              << " status : " << status;
          return;
        }
      }
      auto new_term = consensus->ConsensusState(CONSENSUS_CONFIG_ACTIVE).current_term();
      if (new_term != term) {
        LOG_WITH_PREFIX(INFO)
            << "Error loading sys catalog; but that's OK as term was changed from " << term
            << " to " << new_term << ": " << status;
        return;
      }
      LOG_WITH_PREFIX(FATAL) << "Failed to load sys catalog: " << status;
    }
  }

  {
    std::lock_guard l(state_lock_);
    leader_ready_term_ = term;
    is_catalog_loaded_ = true;
    LOG_WITH_PREFIX(INFO) << "Completed load of sys catalog in term " << term;
  }

  // Finalize state and do post loading work.
  SysCatalogLoaded(std::move(state));

  // Once we have loaded the SysCatalog, reset and regenerate the yql partitions table in order to
  // regenerate entries for previous tables.
  GetYqlPartitionsVtable().ResetAndRegenerateCache();
}

Status CatalogManager::WaitForWorkerPoolTests(const MonoDelta& timeout) const {
  if (!async_task_pool_->WaitFor(timeout)) {
    return STATUS(TimedOut, "Worker Pool hasn't finished processing tasks");
  }
  return Status::OK();
}

Status CatalogManager::GetTableDiskSize(const GetTableDiskSizeRequestPB* req,
                                        GetTableDiskSizeResponsePB *resp,
                                        rpc::RpcContext* rpc) {
  auto table_id = req->table().table_id();

  const auto table_info = GetTableInfo(table_id);
  if (!table_info) {
    auto s = STATUS_SUBSTITUTE(NotFound, "Table with id $0 does not exist", table_id);
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  int64 table_size = 0;
  int32 num_missing_tablets = 0;
  if (!table_info->IsColocatedUserTable()) {
    // Colocated user tables do not have size info

    // Set missing tablets if test flag is set
    num_missing_tablets =
        PREDICT_FALSE(FLAGS_TEST_num_missing_tablets > 0) ? FLAGS_TEST_num_missing_tablets : 0;

    const auto tablets = VERIFY_RESULT(table_info->GetTablets());
    for (const auto& tablet : tablets) {
      const auto drive_info_result = tablet->GetLeaderReplicaDriveInfo();
      if (drive_info_result.ok()) {
        const auto& drive_info = drive_info_result.get();
        VLOG(4)
            << "tablet " << tablet->ToString()
            << " WAL file size: " << drive_info.wal_files_size
            << "\ntablet " << tablet->ToString()
            << " SST file size: " << drive_info.sst_files_size;

        table_size += drive_info.wal_files_size;
        table_size += drive_info.sst_files_size;
      } else {
        ++num_missing_tablets;
      }
    }
  }

  resp->set_size(table_size);
  resp->set_num_missing_tablets(num_missing_tablets);

  return Status::OK();
}

Status CatalogManager::MaybeRestoreInitialSysCatalogSnapshotAndReloadSysCatalog(
    SysCatalogLoadingState* state) {
  if (!FLAGS_enable_ysql || FLAGS_initial_sys_catalog_snapshot_path.empty() ||
      FLAGS_create_initial_sys_catalog_snapshot) {
    return Status::OK();
  }
  if (!master_->fs_manager()->initdb_done_set_after_sys_catalog_restore()) {
    // Since this field is not set, this means that is an existing cluster created without
    // D19510. So skip restoring sys catalog.
    LOG_WITH_PREFIX(INFO)
        << "This is an existing cluster, not initializing from a sys catalog snapshot.";
    return Status::OK();
  }
  // This is a cluster created with D19510, so check the value of initdb_done.
  Result<bool> dir_exists = Env::Default()->IsDirectory(FLAGS_initial_sys_catalog_snapshot_path);
  if (!dir_exists.ok() || !dir_exists.get()) {
    LOG_WITH_PREFIX(WARNING) << "Initial sys catalog snapshot directory does not exist: "
                             << FLAGS_initial_sys_catalog_snapshot_path
                             << (dir_exists.ok() ? ", path is not a directory"
                                                 : ", status: " + dir_exists.status().ToString());
    return Status::OK();
  }

  if (ysql_manager_->IsInitDbDone()) {
    LOG_WITH_PREFIX(INFO) << "initdb has been run before, no need to restore sys catalog from "
                          << "the initial snapshot";
    return Status::OK();
  }

  LOG_WITH_PREFIX(INFO) << "Restoring snapshot in sys catalog";
  if (GetAtomicFlag(&FLAGS_master_join_existing_universe)) {
    return STATUS(
        IllegalState,
        "Master is joining an existing universe but wants to restore initial sys catalog snapshot. "
        "This should have been done during initial universe creation.");
  }
  RETURN_NOT_OK_PREPEND(
      RestoreInitialSysCatalogSnapshot(
          FLAGS_initial_sys_catalog_snapshot_path, sys_catalog_->tablet_peer().get(),
          state->epoch.leader_term),
      "Failed restoring snapshot in sys catalog");
  LOG_WITH_PREFIX(INFO) << "Re-initializing cluster config";
  cluster_config_.reset();
  RETURN_NOT_OK(PrepareDefaultClusterConfig(state->epoch.leader_term));

  LOG_WITH_PREFIX(INFO) << "Re-initializing xcluster config";
  RETURN_NOT_OK(xcluster_manager_->PrepareDefaultXClusterConfig(
      state->epoch.leader_term, /* recreate = */ true));

  LOG_WITH_PREFIX(INFO) << "Restoring snapshot completed, considering initdb finished";
  RETURN_NOT_OK(ysql_manager_->SetInitDbDone(state->epoch));
  // TODO(asrivastava): Can we get rid of this Reset() by calling RunLoaders just once
  // instead of calling it here and in VisitSysCatalog?
  state->Reset();
  RETURN_NOT_OK(RunLoaders(state));
  return Status::OK();
}

void CatalogManager::ValidateIndexTablesPostLoad(
    std::unordered_map<TableId, TableIdSet>&& indexes_map, TableIdSet* tables_to_persist) {
  for (auto& [table_id, indexes] : indexes_map) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << "indexes to validate: " << yb::ToString(indexes);
    GetBackfillStatus(
        table_id, std::move(indexes),
        [this, tables_to_persist = DCHECK_NOTNULL(tables_to_persist)](
            const Status& status, const TableId& index_id,
            IndexStatusPB::BackfillStatus backfill_status) {
          DCHECK(status.ok());
          if (!status.ok()) {
            LOG(ERROR) << "ValidateIndexTablesPostLoad: Failed to get backfill status for "
                       << "index table " << index_id << ": " << status;
            return;
          }

          // We are interested only in index with backfilling successfully completed.
          if (backfill_status != IndexStatusPB::BACKFILL_SUCCESS) {
            return;
          }

          // Update in-memory state, persisting to the sys catalog / disk will be done later.
          auto index_table = CHECK_RESULT(GetTableById(index_id));
          auto index_table_wlock = index_table->LockForWrite();
          BackfillTable::UnsetIndexTableRetainsDeleteMarkers(index_table_wlock.mutable_data());
          index_table_wlock.Commit();
          tables_to_persist->emplace(index_id);
        });
  }
}

Status CatalogManager::VisitSysCatalog(SysCatalogLoadingState* state) {
  // Block new catalog operations, and wait for existing operations to finish.
  int64_t term = state->epoch.leader_term;
  LOG_WITH_PREFIX_AND_FUNC(INFO)
    << "Wait on leader_lock_ for any existing operations to finish. Term: " << term;
  auto start = std::chrono::steady_clock::now();
  std::lock_guard leader_lock_guard(leader_lock_);
  auto finish = std::chrono::steady_clock::now();

  static const auto kLongLockAcquisitionLimit = RegularBuildVsSanitizers(100ms, 750ms);
  if (finish > start + kLongLockAcquisitionLimit) {
    LOG_WITH_PREFIX(WARNING) << "Long wait on leader_lock_: " << yb::ToString(finish - start);
  }

  // Exclusive mutex_ scope.
  {
    LOG_WITH_FUNC(INFO) << "Acquire catalog manager lock_ before loading sys catalog.";
    LockGuard lock(mutex_);
    VLOG_WITH_FUNC(3) << "Acquired the catalog manager lock";

    // Abort any outstanding tasks. All CatalogEntities are orphaned below, so
    // it's important to end their tasks now.
    //
    // N.B. This doesn't actually wait for anything at all.
    // See https://github.com/yugabyte/yugabyte-db/issues/18634
    //
    // The async task framework provides a hook, the Finished method, for subclasses to perform
    // additional actions when the task has finished its main work. This method is also called when
    // a task is aborted. Some tasks ie. tablet snapshot tasks call catalog manager methods that
    // acquire the mutex_. Because we don't use reentrant mutexes this deadlocks the server. So we
    // pass a boolean parameter here to the async task framework to skip calling the Finished
    // method.
    AbortAndWaitForAllTasksUnlocked(/* call_task_finisher */ false);

    if (FLAGS_enable_ysql) {
      // Number of TS to wait for before creating the txn table.
      auto wait_ts_count = std::max(FLAGS_txn_table_wait_min_ts_count, FLAGS_replication_factor);

      LOG_WITH_PREFIX(INFO) << "YSQL is enabled, will create the transaction status table when "
                            << wait_ts_count << " tablet servers are online";
      master_->ts_manager()->SetTSCountCallback(
          wait_ts_count, [this, wait_ts_count, local_epoch = state->epoch] {
            LOG_WITH_PREFIX(INFO)
                << wait_ts_count
                << " tablet servers registered, creating the transaction status table";
            // Retry table creation until it succeedes. It might fail initially because placement
            // UUID of live replicas is set through an RPC from YugaWare, and we won't be able to
            // calculate the number of primary (non-read-replica) tablet servers until that happens.
            while (true) {
              const auto s =
                  CreateGlobalTransactionStatusTableIfNotPresent(/* rpc */ nullptr, local_epoch);
              if (s.ok()) {
                break;
              }
              LOG_WITH_PREFIX(WARNING)
                  << "Failed creating transaction status table, waiting: " << s;
              if (s.IsShutdownInProgress()) {
                return;
              }
              auto role = Role();
              if (role != PeerRole::LEADER) {
                LOG_WITH_PREFIX(WARNING)
                    << "Cancel creating transaction because of role: " << PeerRole_Name(role);
                return;
              }
              SleepFor(MonoDelta::FromSeconds(1));
            }
            LOG_WITH_PREFIX(INFO) << "Finished creating transaction status table asynchronously";
          });
    }

    // Clear internal maps and run data loaders.
    RETURN_NOT_OK(RunLoaders(state));

    // Prepare various default system configurations.
    RETURN_NOT_OK(PrepareDefaultSysConfig(term));

    RETURN_NOT_OK(ysql_manager_->PrepareDefaultSysConfig(state->epoch));

    RETURN_NOT_OK(MaybeRestoreInitialSysCatalogSnapshotAndReloadSysCatalog(state));

    // Create the system namespaces (created only if they don't already exist).
    RETURN_NOT_OK(PrepareDefaultNamespaces(term));

    // Create the system tables (created only if they don't already exist).
    RETURN_NOT_OK(PrepareSystemTables(state->epoch));

    // Create the default cassandra (created only if they don't already exist).
    RETURN_NOT_OK(permissions_manager_->PrepareDefaultRoles(term));

    // If this is the first time we start up, we have no config information as default. We write an
    // empty version 0.
    RETURN_NOT_OK(PrepareDefaultClusterConfig(term));

    RETURN_NOT_OK(xcluster_manager_->PrepareDefaultXClusterConfig(term, /* recreate = */ false));

    permissions_manager_->BuildRecursiveRoles();

    if (!VERIFY_RESULT(ysql_manager_->StartRunningInitDbIfNeeded(state->epoch))) {
      // If we are not running initdb, this is an existing cluster, and we need to check whether we
      // need to do a one-time migration to make YSQL system catalog tables transactional.
      RETURN_NOT_OK(MakeYsqlSysCatalogTablesTransactional(
          tables_->GetAllTables(), sys_catalog_, *ysql_manager_.get(), state->epoch));
    }
  }  // Exclusive mutex_ scope.
  return Status::OK();
}

template <class Loader>
Status CatalogManager::Load(
    const std::string& title, SysCatalogLoadingState* state) {
  LOG_WITH_PREFIX(INFO) << __func__ << ": Loading " << title << " into memory.";
  std::unique_ptr<Loader> loader = std::make_unique<Loader>(this, state);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Visit(loader.get()),
      "Failed while visiting " + title + " in sys catalog");
  return Status::OK();
}

Status CatalogManager::RunLoaders(SysCatalogLoadingState* state) {
  // Clear the table and tablet state.
  table_names_map_.clear();
  transaction_table_ids_set_.clear();
  auto table_map_checkout = tables_.CheckOut();
  table_map_checkout->Clear();

  auto tablet_map_checkout = tablet_map_.CheckOut();
  tablet_map_checkout->clear();

  // Clear the namespace mappings.
  namespace_ids_map_.clear();
  namespace_names_mapper_.clear();

  // Clear the type mappings.
  udtype_ids_map_.clear();
  udtype_names_map_.clear();

  // Clear the current cluster config.
  cluster_config_.reset();

  // Clear redis config mapping.
  redis_config_map_.clear();

  // Clear Object lock mapping.
  object_lock_info_manager_->Clear();

  // Clear transaction tables config.
  transaction_tables_config_.reset();

  // Clear tablegroup mappings.
  tablegroup_manager_.reset(new YsqlTablegroupManager());

  // Clear recent jobs/tasks.
  ResetTasksTrackers();

  ClearXReplState();

  // Clear Ddl transaction state.
  {
    LockGuard l(ddl_txn_verifier_mutex_);
    ysql_ddl_txn_verfication_state_map_.clear();
  }

  ysql_manager_->Clear();
  xcluster_manager_->Clear();

  // This is unnecessary if persist_tserver_registry is set.
  // But persist_tserver_registry is a runtime flag so we keep this here to
  // simplify system behaviour if the flag is changed during runtime.
  auto descs = master_->ts_manager()->GetAllDescriptors();
  for (const auto& ts_desc : descs) {
    ts_desc->set_has_tablet_report(false);
  }

  {
    LockGuard lock(permissions_manager()->mutex());

    // Clear the roles mapping.
    permissions_manager()->ClearRolesUnlocked();
    RETURN_NOT_OK(Load<RoleLoader>("roles", state));
    RETURN_NOT_OK(Load<SysConfigLoader>("sys config", state));
  }
  // Clear the hidden tablets vector.
  hidden_tablets_.clear();

  deleted_tablets_loaded_from_sys_catalog_.clear();

  RETURN_NOT_OK(Load<NamespaceLoader>("namespaces", state));
  RETURN_NOT_OK(Load<TableLoader>("tables", state));
  RETURN_NOT_OK(Load<TabletLoader>("tablets", state));
  RETURN_NOT_OK(Load<UDTypeLoader>("user-defined types", state));
  RETURN_NOT_OK(Load<ClusterConfigLoader>("cluster configuration", state));
  RETURN_NOT_OK(Load<RedisConfigLoader>("Redis config", state));
  RETURN_NOT_OK(Load<ObjectLockLoader>("Object locks", state));

  if (!transaction_tables_config_) {
    RETURN_NOT_OK(InitializeTransactionTablesConfig(state->epoch.leader_term));
  }

  RETURN_NOT_OK(LoadXReplStream());
  RETURN_NOT_OK(LoadUniverseReplication());
  RETURN_NOT_OK(LoadUniverseReplicationBootstrap());

  RETURN_NOT_OK(xcluster_manager_->RunLoaders(hidden_tablets_));
  RETURN_NOT_OK(master_->clone_state_manager().ClearAndRunLoaders(state->epoch));
  RETURN_NOT_OK(master_->ts_manager()->RunLoader(
      master_->MakeCloudInfoPB(), &master_->proxy_cache(), *state));

  return Status::OK();
}

bool CatalogManager::IsDeletedTabletLoadedFromSysCatalog(const TabletId& tablet_id) const {
  SharedLock lock(mutex_);
  return deleted_tablets_loaded_from_sys_catalog_.contains(tablet_id);
}

Status CatalogManager::CheckResource(
    const GrantRevokePermissionRequestPB* req,
    GrantRevokePermissionResponsePB* resp) {
  scoped_refptr<TableInfo> table;

  // Checking if resources exist.
  if (req->resource_type() == ResourceType::TABLE ||
      req->resource_type() == ResourceType::KEYSPACE) {
    // We can't match Apache Cassandra's error because when a namespace is not provided, the error
    // is detected by the semantic analysis in PTQualifiedName::AnalyzeName.
    DCHECK(req->has_namespace_());
    const auto& namespace_info = req->namespace_();
    auto ns = FindNamespace(namespace_info);

    if (req->resource_type() == ResourceType::KEYSPACE) {
      if (!ns.ok()) {
        // Matches Apache Cassandra's error.
        Status s = STATUS_SUBSTITUTE(
            NotFound, "Resource <keyspace $0> doesn't exist", namespace_info.name());
        return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
      }
    } else {
      if (ns.ok()) {
        SharedLock l(mutex_);
        table = FindPtrOrNull(table_names_map_, {(**ns).id(), req->resource_name()});
      }
      if (table == nullptr) {
        // Matches Apache Cassandra's error.
        Status s = STATUS_SUBSTITUTE(
            NotFound, "Resource <object '$0.$1'> doesn't exist",
            namespace_info.name(), req->resource_name());
        return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
      }
    }
  }
  return Status::OK();
}

Status CatalogManager::PrepareDefaultClusterConfig(int64_t term) {
  if (cluster_config_) {
    LOG_WITH_PREFIX(INFO)
        << "Cluster configuration has already been set up, skipping re-initialization.";
    return Status::OK();
  }

  // Create default.
  SysClusterConfigEntryPB config;
  config.set_version(0);

  std::string cluster_uuid_source;
  if (!FLAGS_cluster_uuid.empty()) {
    RETURN_NOT_OK(Uuid::FromString(FLAGS_cluster_uuid));
    config.set_cluster_uuid(FLAGS_cluster_uuid);
    cluster_uuid_source = "from the --cluster_uuid flag";
  } else {
    auto uuid = Uuid::Generate();
    config.set_cluster_uuid(uuid.ToString());
    cluster_uuid_source = "(randomly generated)";
  }
  LOG_WITH_PREFIX(INFO)
      << "Setting cluster UUID to " << config.cluster_uuid() << " " << cluster_uuid_source;

  if (GetAtomicFlag(&FLAGS_master_enable_universe_uuid_heartbeat_check)) {
    auto universe_uuid = Uuid::Generate().ToString();
    LOG_WITH_PREFIX(INFO) << Format("Setting universe_uuid to $0 on new universe", universe_uuid);
    config.set_universe_uuid(universe_uuid);
  }

  // Create in memory object.
  cluster_config_ = std::make_shared<ClusterConfigInfo>();

  // Prepare write.
  auto l = cluster_config_->LockForWrite();
  l.mutable_data()->pb = std::move(config);

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->Upsert(term, cluster_config_.get()));
  l.Commit();

  return Status::OK();
}

Status CatalogManager::SetUniverseUuidIfNeeded(const LeaderEpoch& epoch) {
  if (!GetAtomicFlag(&FLAGS_master_enable_universe_uuid_heartbeat_check)) {
    return Status::OK();
  }

  auto cluster_config = ClusterConfig();
  SCHECK(cluster_config, IllegalState, "Cluster config is not initialized");

  auto l = cluster_config->LockForWrite();
  if (!l.data().pb.universe_uuid().empty()) {
    return Status::OK();
  }

  auto universe_uuid = Uuid::Generate().ToString();
  LOG_WITH_PREFIX(INFO) << Format("Setting universe_uuid to $0 on existing universe",
                                  universe_uuid);

  l.mutable_data()->pb.set_universe_uuid(universe_uuid);
  l.mutable_data()->pb.set_version(l.mutable_data()->pb.version() + 1);
  RETURN_NOT_OK(sys_catalog_->Upsert(epoch, cluster_config_.get()));
  l.Commit();
  return Status::OK();
}

Status CatalogManager::GetUniverseKeyRegistryFromOtherMastersAsync() {
  RETURN_NOT_OK_PREPEND(sys_catalog_->WaitUntilRunning(),
                      "Failed waiting for the catalog tablet to run");
  std::vector<consensus::RaftPeerPB> masters_raft;
  RETURN_NOT_OK(master_->ListRaftConfigMasters(&masters_raft));
  std::vector<HostPort> hps;
  for (const auto& peer : masters_raft) {
    if (NodeInstance().permanent_uuid() == peer.permanent_uuid()) {
      continue;
    }
    HostPort hp = HostPortFromPB(DesiredHostPort(peer, master_->MakeCloudInfoPB()));
    hps.push_back(hp);
  }
  if (!universe_key_client_) {
    universe_key_client_ = std::make_unique<client::UniverseKeyClient>(
        hps, &master_->proxy_cache(), [&] (const encryption::UniverseKeysPB& universe_keys) {
          encryption_manager_->PopulateUniverseKeys(universe_keys);
        });
  }
  universe_key_client_->GetUniverseKeyRegistryAsync();
  return Status::OK();
}

Result<std::vector<HostPort>> CatalogManager::GetMasterAddressHostPorts() {
  std::vector<HostPort> result;
  consensus::ConsensusStatePB state;
  RETURN_NOT_OK(GetCurrentConfig(&state));

  for (const auto& peer : state.config().peers()) {
    HostPortsFromPBs(peer.last_known_private_addr(), &result);
    HostPortsFromPBs(peer.last_known_broadcast_addr(), &result);
  }
  return result;
}

std::vector<std::string> CatalogManager::GetMasterAddresses() {
  std::vector<std::string> result;
  consensus::ConsensusStatePB state;
  auto status = GetCurrentConfig(&state);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to get current config: " << status;
    return result;
  }
  for (const auto& peer : state.config().peers()) {
    std::vector<std::string> peer_addresses;
    for (const auto& list : {peer.last_known_private_addr(), peer.last_known_broadcast_addr()}) {
      for (const auto& entry : list) {
        peer_addresses.push_back(HostPort::FromPB(entry).ToString());
      }
    }
    if (!peer_addresses.empty()) {
      result.push_back(JoinStrings(peer_addresses, ","));
    }
  }
  return result;
}

Status CatalogManager::PrepareDefaultSysConfig(int64_t term) {
  {
    LockGuard lock(permissions_manager()->mutex());
    RETURN_NOT_OK(permissions_manager()->PrepareDefaultSecurityConfigUnlocked(term));
  }

  if (!transaction_tables_config_) {
    RETURN_NOT_OK(InitializeTransactionTablesConfig(term));
  }

  return Status::OK();
}

Status CatalogManager::PrepareDefaultNamespaces(int64_t term) {
  RETURN_NOT_OK(PrepareNamespace(
      YQL_DATABASE_CQL, kSystemNamespaceName, kSystemNamespaceId, term));
  RETURN_NOT_OK(PrepareNamespace(
      YQL_DATABASE_CQL, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, term));
  RETURN_NOT_OK(PrepareNamespace(
      YQL_DATABASE_CQL, kSystemAuthNamespaceName, kSystemAuthNamespaceId, term));
  return Status::OK();
}

Status CatalogManager::PrepareSystemTables(const LeaderEpoch& epoch) {
  // Prepare sys catalog table.
  RETURN_NOT_OK(PrepareSysCatalogTable(epoch));

  // Create the required system tables here.
  RETURN_NOT_OK((PrepareSystemTableTemplate<PeersVTable>(
      kSystemPeersTableName, kSystemNamespaceName, kSystemNamespaceId, epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<LocalVTable>(
      kSystemLocalTableName, kSystemNamespaceName, kSystemNamespaceId, epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLKeyspacesVTable>(
      kSystemSchemaKeyspacesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
      epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLTablesVTable>(
      kSystemSchemaTablesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLColumnsVTable>(
      kSystemSchemaColumnsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLSizeEstimatesVTable>(
      kSystemSizeEstimatesTableName, kSystemNamespaceName, kSystemNamespaceId, epoch)));

  // Empty tables.
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLAggregatesVTable>(
      kSystemSchemaAggregatesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
      epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLFunctionsVTable>(
      kSystemSchemaFunctionsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
      epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLIndexesVTable>(
      kSystemSchemaIndexesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLTriggersVTable>(
      kSystemSchemaTriggersTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId,
      epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLViewsVTable>(
      kSystemSchemaViewsTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<QLTypesVTable>(
      kSystemSchemaTypesTableName, kSystemSchemaNamespaceName, kSystemSchemaNamespaceId, epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLPartitionsVTable>(
      kSystemPartitionsTableName, kSystemNamespaceName, kSystemNamespaceId, epoch)));

  // System auth tables.
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLAuthRolesVTable>(
      kSystemAuthRolesTableName, kSystemAuthNamespaceName, kSystemAuthNamespaceId, epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLAuthRolePermissionsVTable>(
      kSystemAuthRolePermissionsTableName, kSystemAuthNamespaceName, kSystemAuthNamespaceId,
      epoch)));
  RETURN_NOT_OK((PrepareSystemTableTemplate<YQLAuthResourceRolePermissionsIndexVTable>(
      kSystemAuthResourceRolePermissionsIndexTableName, kSystemAuthNamespaceName,
      kSystemAuthNamespaceId, epoch)));

  // Ensure kNumSystemTables is in-sync with the system tables created.
  LOG_IF(DFATAL, system_tablets_.size() != kNumSystemTables)
      << "kNumSystemTables is " << kNumSystemTables << " but " << system_tablets_.size()
      << " tables were created";

  // Cache the system.partitions tablet so we can access it in RebuildYQLSystemPartitions.
  RETURN_NOT_OK(GetYQLPartitionsVTable(&system_partitions_tablet_));

  return Status::OK();
}

Status CatalogManager::PrepareSysCatalogTable(const LeaderEpoch& epoch) {
  auto sys_catalog_tablet = VERIFY_RESULT(sys_catalog_->tablet_peer_->shared_tablet_safe());

  // Prepare sys catalog table info.
  auto sys_catalog_table = tables_->FindTableOrNull(kSysCatalogTableId);
  if (sys_catalog_table == nullptr) {
    scoped_refptr<TableInfo> table = NewTableInfo(kSysCatalogTableId, false);
    table->mutable_metadata()->StartMutation();
    SysTablesEntryPB& metadata = table->mutable_metadata()->mutable_dirty()->pb;
    metadata.set_state(SysTablesEntryPB::RUNNING);
    metadata.set_namespace_id(kSystemSchemaNamespaceId);
    metadata.set_namespace_name(kSystemSchemaNamespaceName);
    metadata.set_name(kSysCatalogTableName);
    metadata.set_table_type(TableType::YQL_TABLE_TYPE);
    SchemaToPB(sys_catalog_->schema(), metadata.mutable_schema());
    metadata.set_version(0);

    auto table_map_checkout = tables_.CheckOut();
    table_map_checkout->AddOrReplace(table);
    sys_catalog_table = table;
    table_names_map_[{kSystemSchemaNamespaceId, kSysCatalogTableName}] = table;
    table->set_is_system();

    RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table));
    table->mutable_metadata()->CommitMutation();
  }

  // Prepare sys catalog tablet info.
  if (tablet_map_->count(kSysCatalogTabletId) == 0) {
    auto tablet = std::make_shared<TabletInfo>(sys_catalog_table, kSysCatalogTabletId);
    tablet->mutable_metadata()->StartMutation();
    SysTabletsEntryPB& metadata = tablet->mutable_metadata()->mutable_dirty()->pb;
    metadata.set_state(SysTabletsEntryPB::RUNNING);

    auto l = sys_catalog_table->LockForRead();
    PartitionSchema partition_schema;
    RETURN_NOT_OK(PartitionSchema::FromPB(
        l->pb.partition_schema(), sys_catalog_->schema(), &partition_schema));
    vector<Partition> partitions;
    RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));
    partitions[0].ToPB(metadata.mutable_partition());
    metadata.set_table_id(sys_catalog_table->id());
    metadata.add_table_ids(sys_catalog_table->id());

    sys_catalog_table->set_is_system();
    RETURN_NOT_OK(sys_catalog_table->AddTablet(tablet));

    auto tablet_map_checkout = tablet_map_.CheckOut();
    (*tablet_map_checkout)[tablet->tablet_id()] = tablet;

    RETURN_NOT_OK(sys_catalog_->Upsert(epoch, tablet));
    tablet->mutable_metadata()->CommitMutation();
    // todo(zdrudi): to support the new schema for the sys tablet, add the kSysCatalogTableId to the
    // tablet's in memory list of table ids.
  }

  system_tablets_[kSysCatalogTabletId] = sys_catalog_tablet;

  return Status::OK();
}

template <class T>
Status CatalogManager::PrepareSystemTableTemplate(const TableName& table_name,
                                                  const NamespaceName& namespace_name,
                                                  const NamespaceId& namespace_id,
                                                  const LeaderEpoch& epoch) {
  YQLVirtualTable* vtable = new T(table_name, namespace_name, master_);
  return PrepareSystemTable(
      table_name, namespace_name, namespace_id, vtable->schema(), epoch, vtable);
}

Status CatalogManager::PrepareSystemTable(const TableName& table_name,
                                          const NamespaceName& namespace_name,
                                          const NamespaceId& namespace_id,
                                          const Schema& schema,
                                          const LeaderEpoch& epoch,
                                          YQLVirtualTable* vtable) {
  std::unique_ptr<YQLVirtualTable> yql_storage(vtable);

  scoped_refptr<TableInfo> table = FindPtrOrNull(table_names_map_,
                                                 std::make_pair(namespace_id, table_name));
  bool create_table = true;
  if (table != nullptr) {
    LOG_WITH_PREFIX(INFO) << "Table " << namespace_name << "." << table_name << " already created";

    // Mark the table as a system table.
    table->set_is_system();

    auto persisted_schema = VERIFY_RESULT(table->GetSchema());
    if (!persisted_schema.Equals(schema)) {
      LOG_WITH_PREFIX(INFO)
          << "Updating schema of " << namespace_name << "." << table_name << " ...";
      auto l = table->LockForWrite();
      SchemaToPB(schema, l.mutable_data()->pb.mutable_schema());
      l.mutable_data()->pb.set_version(l->pb.version() + 1);
      l.mutable_data()->pb.set_updates_only_index_permissions(false);

      // Update sys-catalog with the new table schema.
      RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table));
      l.Commit();
    }

    // There might have been a failure after writing the table but before writing the tablets. As
    // a result, if we don't find any tablets, we try to create the tablets only again.
    auto tablets = VERIFY_RESULT(table->GetTablets());
    if (!tablets.empty()) {
      // Initialize the appropriate system tablet.
      DCHECK_EQ(1, tablets.size());
      auto tablet = tablets[0];
      system_tablets_[tablet->tablet_id()] =
          std::make_shared<SystemTablet>(schema, std::move(yql_storage), tablet->tablet_id());
      return Status::OK();
    } else {
      // Table is already created, only need to create tablets now.
      LOG_WITH_PREFIX(INFO)
          << "Creating tablets for " << namespace_name << "." << table_name << " ...";
      create_table = false;
    }
  }

  // Create partitions.
  vector<Partition> partitions;
  PartitionSchemaPB partition_schema_pb;
  partition_schema_pb.set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
  PartitionSchema partition_schema;
  RETURN_NOT_OK(PartitionSchema::FromPB(partition_schema_pb, schema, &partition_schema));
  RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));

  TabletInfos tablets;

  if (create_table) {
    // Fill in details for the system table.
    CreateTableRequestPB req;
    req.set_name(table_name);
    req.set_table_type(TableType::YQL_TABLE_TYPE);

    RETURN_NOT_OK(CreateTableInMemory(
        req, schema, partition_schema, namespace_id, namespace_name, partitions,
        /* colocated */ false, IsSystemObject::kTrue, nullptr, &tablets, nullptr,
        &table));
    // Mark the table as a system table.
    LOG_WITH_PREFIX(INFO) << "Inserted new " << namespace_name << "." << table_name
                          << " table info into CatalogManager maps";
    // Update the on-disk table state to "running".
    table->mutable_metadata()->mutable_dirty()->pb.set_state(SysTablesEntryPB::RUNNING);
    RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table));
    LOG_WITH_PREFIX(INFO) << Format(
        "Wrote table to system catalog: $0, tablets: $1", table, tablets);
  } else {
    // Still need to create the tablets.
    tablets = VERIFY_RESULT(CreateTabletsFromTable(partitions, table));
  }

  DCHECK_EQ(1, tablets.size());
  // We use LOG_ASSERT here since this is expected to crash in some unit tests.
  LOG_ASSERT(!FLAGS_TEST_catalog_manager_simulate_system_table_create_failure);

  // Write Tablets to sys-tablets (in "running" state since we don't want the loadbalancer to
  // assign these tablets since this table is virtual).
  for (const auto& tablet : tablets) {
    tablet->mutable_metadata()->mutable_dirty()->pb.set_state(SysTabletsEntryPB::RUNNING);
  }
  RETURN_NOT_OK(sys_catalog_->Upsert(epoch, tablets));
  LOG_WITH_PREFIX(INFO) << Format("Wrote tablets to system catalog: $0", tablets);

  // Commit the in-memory state.
  if (create_table) {
    table->mutable_metadata()->CommitMutation();
  }

  for (const auto& tablet : tablets) {
    tablet->mutable_metadata()->CommitMutation();
  }

  // Finally create the appropriate tablet object.
  auto tablet = tablets[0];
  system_tablets_[tablet->tablet_id()] =
      std::make_shared<SystemTablet>(schema, std::move(yql_storage), tablet->tablet_id());
  return Status::OK();
}

bool IsYcqlNamespace(const NamespaceInfo& ns) {
  return ns.database_type() == YQLDatabase::YQL_DATABASE_CQL;
}

bool IsYcqlTable(const TableInfo& table) {
  return table.GetTableType() == TableType::YQL_TABLE_TYPE && table.id() != kSysCatalogTableId;
}

Status CatalogManager::PrepareNamespace(
    YQLDatabase db_type, const NamespaceName& name, const NamespaceId& id, int64_t term) {

  scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, id);
  if (ns != nullptr) {
    LOG_WITH_PREFIX(INFO)
        << "Keyspace " << ns->ToString() << " already created, skipping initialization";
    return Status::OK();
  }

  // Create entry.
  SysNamespaceEntryPB ns_entry;
  ns_entry.set_name(name);
  ns_entry.set_database_type(db_type);
  ns_entry.set_state(SysNamespaceEntryPB::RUNNING);

  // Create in memory object.
  ns = new NamespaceInfo(id, tasks_tracker_);

  // Prepare write.
  auto l = ns->LockForWrite();
  l.mutable_data()->pb = std::move(ns_entry);

  namespace_ids_map_[id] = ns;
  namespace_names_mapper_[db_type][l.mutable_data()->pb.name()] = ns;

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->Upsert(term, ns));
  l.Commit();

  LOG_WITH_PREFIX(INFO) << "Created default keyspace: " << ns->ToString();
  return Status::OK();
}

Status CatalogManager::CheckLocalHostInMasterAddresses() {
  auto local_hostport = master_->first_rpc_address();
  std::vector<IpAddress> local_addrs;

  if (local_hostport.address().is_unspecified()) {
    auto status = GetLocalAddresses(&local_addrs, AddressFilter::ANY);
    if (!status.ok() || local_addrs.empty()) {
      LOG(WARNING) << "Could not enumerate network interfaces due to " << status << ", found "
                   << local_addrs.size() << " local addresses.";
      return Status::OK();
    }
  } else {
    for (auto const &addr : master_->rpc_addresses()) {
      local_addrs.push_back(addr.address());
    }
  }

  auto broadcast_addresses = master_->opts().broadcast_addresses;
  if (!broadcast_addresses.empty()) {
    auto resolved_broadcast_addresses = VERIFY_RESULT(server::ResolveMasterAddresses(
        {broadcast_addresses}));
    for (auto const &addr : resolved_broadcast_addresses) {
      local_addrs.push_back(addr.address());
    }
  }

  auto resolved_addresses = VERIFY_RESULT(server::ResolveMasterAddresses(
      *master_->opts().GetMasterAddresses()));

  for (auto const &addr : resolved_addresses) {
    if (addr.address().is_unspecified() ||
        std::find(local_addrs.begin(), local_addrs.end(), addr.address()) !=
            local_addrs.end()) {
      return Status::OK();
    }
  }
  return STATUS_SUBSTITUTE(IllegalState,
      "None of the local addresses are present in master_addresses $0.",
      master_->opts().master_addresses_flag);
}

Status CatalogManager::InitSysCatalogAsync() {
  LockGuard lock(mutex_);

  // Optimistically try to load data from disk.
  Status s = sys_catalog_->Load(master_->fs_manager());
  if (s.ok() || !s.IsNotFound()) { return s; }
  LOG(INFO) << "Did not find previous SysCatalogTable data on disk. " << s;

  // Given loading the system catalog failed with NotFound we must decide
  // whether to enter shell mode in order to join an existing universe or to create a fresh, empty
  // system catalog for a new universe.
  if (GetAtomicFlag(&FLAGS_master_join_existing_universe) ||
      !master_->opts().AreMasterAddressesProvided()) {
    // We unconditionally enter shell mode if master_join_existing_universe is true.
    // Otherwise we determine whether or not master_addresses is empty to decide to enter shell
    // mode.
    master_->SetShellMode(true);
    LOG(INFO) << "Starting master in shell mode.";
    return Status::OK();
  } else {
    // master_join_existing_universe is false and master_addresses is set. This is how operators
    // tell a set of master processes with empty on-disk state to create a new universe.

    RETURN_NOT_OK(CheckLocalHostInMasterAddresses());
    RETURN_NOT_OK_PREPEND(
        sys_catalog_->CreateNew(master_->fs_manager()),
        Substitute(
            "Encountered errors during system catalog initialization:"
            "\n\tError on Load: $0\n\tError on CreateNew: ",
            s.ToString()));

    return Status::OK();
  }
}

bool CatalogManager::IsInitialized() const {
  std::lock_guard l(state_lock_);
  return state_ == kRunning;
}

Status CatalogManager::CheckIsLeaderAndReady() const {
  {
    std::lock_guard l(state_lock_);
    if (PREDICT_FALSE(state_ != kRunning)) {
      return STATUS_SUBSTITUTE(ServiceUnavailable,
          "Catalog manager is shutting down. State: $0", state_);
    }
  }

  string uuid = master_->fs_manager()->uuid();
  if (master_->opts().IsShellMode()) {
    // Consensus and other internal fields should not be checked when is shell mode.
    return STATUS_SUBSTITUTE(IllegalState,
        "Catalog manager of $0 is in shell mode, not the leader", uuid);
  }
  auto consensus = VERIFY_RESULT(tablet_peer()->GetConsensus());
  ConsensusStatePB cstate = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED);
  if (PREDICT_FALSE(!cstate.has_leader_uuid() || cstate.leader_uuid() != uuid)) {
    return STATUS_SUBSTITUTE(IllegalState,
        "Not the leader. Local UUID: $0, Consensus state: $1", uuid, cstate.ShortDebugString());
  }

  {
    std::lock_guard l(state_lock_);
    if (PREDICT_FALSE(leader_ready_term_ != cstate.current_term())) {
      return STATUS_SUBSTITUTE(ServiceUnavailable,
          "Leader not yet ready to serve requests: ready term $0 vs cstate term $1",
          leader_ready_term_, cstate.current_term());
    }
  }
  return Status::OK();
}

Status CatalogManager::IsEpochValid(const LeaderEpoch& epoch) const {
  SCHECK_EQ(epoch, GetLeaderEpochInternal(), IllegalState, "Master leader epoch has changed");
  RETURN_NOT_OK(CheckIsLeaderAndReady());
  return Status::OK();
}

std::shared_ptr<tablet::TabletPeer> CatalogManager::tablet_peer() const {
  DCHECK(sys_catalog_);
  return sys_catalog_->tablet_peer();
}

PeerRole CatalogManager::Role() const {
  if (!IsInitialized() || master_->opts().IsShellMode()) {
    return PeerRole::NON_PARTICIPANT;
  }

  auto consensus_result = tablet_peer()->GetConsensus();
  if (!consensus_result) {
    return PeerRole::NON_PARTICIPANT;
  }

  return consensus_result.get()->role();
}

bool CatalogManager::StartShutdown() {
  {
    std::lock_guard l(state_lock_);
    if (state_ == kClosing) {
      VLOG(2) << "CatalogManager already shut down";
      return false;
    }
    state_ = kClosing;
  }

  refresh_yql_partitions_task_.StartShutdown();

  refresh_ysql_tablespace_info_task_.StartShutdown();

  xrepl_parent_tablet_deletion_task_.StartShutdown();

  refresh_ysql_pg_catalog_versions_task_.StartShutdown();

  xcluster_manager_->StartShutdown();

  if (sys_catalog_) {
    sys_catalog_->StartShutdown();
  }

  return true;
}

void CatalogManager::CompleteShutdown() {
  refresh_yql_partitions_task_.CompleteShutdown();
  refresh_ysql_tablespace_info_task_.CompleteShutdown();
  xrepl_parent_tablet_deletion_task_.CompleteShutdown();
  refresh_ysql_pg_catalog_versions_task_.CompleteShutdown();
  xcluster_manager_->CompleteShutdown();

  if (background_tasks_) {
    background_tasks_->Shutdown();
  }
  if (background_tasks_thread_pool_) {
    background_tasks_thread_pool_->Shutdown();
  }
  if (leader_initialization_pool_) {
    leader_initialization_pool_->Shutdown();
  }
  if (async_task_pool_) {
    async_task_pool_->Shutdown();
  }

  // It's OK if the visitor adds more entries even after we finish; it won't start any new tasks for
  // those entries.
  AbortAndWaitForAllTasks();

  cdc_state_table_.reset();

  // Shut down the underlying storage for tables and tablets.
  if (sys_catalog_) {
    sys_catalog_->CompleteShutdown();
  }

  ResetTasksTrackers();
}

Status CatalogManager::AbortTableCreation(TableInfo* table,
                                          const TabletInfos& tablets,
                                          const Status& s,
                                          CreateTableResponsePB* resp) {
  LOG(WARNING) << s;

  const TableId table_id = table->id();
  const TableName table_name = table->mutable_metadata()->mutable_dirty()->pb.name();
  const NamespaceId table_namespace_id =
      table->mutable_metadata()->mutable_dirty()->pb.namespace_id();
  vector<string> tablet_ids_to_erase;
  for (const auto& tablet : tablets) {
    tablet_ids_to_erase.push_back(tablet->tablet_id());
  }

  LOG(INFO) << "Aborting creation of table '" << table_name << "', erasing table and tablets (" <<
      JoinStrings(tablet_ids_to_erase, ",") << ") from in-memory state.";

  // Since this is a failed creation attempt, it's safe to just abort
  // all tasks, as (by definition) no tasks may be pending against a
  // table that has failed to successfully create.
  table->CloseAndWaitForAllTasksToAbort();
  {
    LockGuard lock(mutex_);

    // Call AbortMutation() manually, as otherwise the lock won't be released.
    for (const auto& tablet : tablets) {
      tablet->mutable_metadata()->AbortMutation();
    }
    table->mutable_metadata()->AbortMutation();
    auto tablet_map_checkout = tablet_map_.CheckOut();
    for (const TabletId& tablet_id_to_erase : tablet_ids_to_erase) {
      CHECK_EQ(tablet_map_checkout->erase(tablet_id_to_erase), 1)
          << "Unable to erase tablet " << tablet_id_to_erase << " from tablet map.";
    }

    auto table_map_checkout = tables_.CheckOut();
    table_names_map_.erase({table_namespace_id, table_name});  // Not present if PGSQL table.
    CHECK_EQ(table_map_checkout->Erase(table_id), 1)
        << "Unable to erase table with id " << table_id << " from table ids map.";
  }
  if (IsYcqlTable(*table)) {
    // Don't process while holding on to mutex_ (#16109).
    GetYqlPartitionsVtable().RemoveFromCache({table->id()});
  }
  return CheckIfNoLongerLeaderAndSetupError(s, resp);
}

Result<ReplicationInfoPB> CatalogManager::GetTableReplicationInfo(
  const ReplicationInfoPB& table_replication_info,
  const TablespaceId& tablespace_id) {

  if (IsReplicationInfoSet(table_replication_info)) {
    // The table has custom replication info set for it, return it if valid.
    RETURN_NOT_OK(ValidateTableReplicationInfo(table_replication_info));
    return table_replication_info;
  }
  // Table level replication info not set. Check whether the table is
  // associated with a tablespace and if so, return the tablespace
  // replication info.
  if (GetAtomicFlag(&FLAGS_enable_ysql_tablespaces_for_placement)) {
    boost::optional<ReplicationInfoPB> tablespace_pb =
      VERIFY_RESULT(GetTablespaceReplicationInfoWithRetry(tablespace_id));
    if (tablespace_pb) {
      // Return the tablespace placement.
      return tablespace_pb.value();
    }
  }

  // Neither table nor tablespace info set. Return cluster level replication info.
  auto l = ClusterConfig()->LockForRead();
  return l->pb.replication_info();
}

Result<ReplicationInfoPB> CatalogManager::GetTableReplicationInfo(const TableInfoPtr& table) {
  ReplicationInfoPB cluster_replication_info;
  {
    auto l = ClusterConfig()->LockForRead();
    cluster_replication_info = l->pb.replication_info();
  }
  return CatalogManagerUtil::GetTableReplicationInfo(
      table, GetTablespaceManager(), cluster_replication_info);
}

std::shared_ptr<YsqlTablespaceManager> CatalogManager::GetTablespaceManager() const {
  SharedLock lock(tablespace_mutex_);
  return tablespace_manager_;
}

Result<boost::optional<TablespaceId>> CatalogManager::GetTablespaceForTable(
    const scoped_refptr<TableInfo>& table) const {
  auto tablespace_manager = GetTablespaceManager();
  return tablespace_manager->GetTablespaceForTable(table);
}

Result<boost::optional<ReplicationInfoPB>> CatalogManager::GetTablespaceReplicationInfoWithRetry(
  const TablespaceId& tablespace_id) {

  auto tablespace_manager = GetTablespaceManager();
  auto replication_info_result = tablespace_manager->GetTablespaceReplicationInfo(tablespace_id);

  if (replication_info_result) {
    return replication_info_result;
  }

  // We failed to find the tablespace placement policy. Refresh the tablespace info and try again.
  auto tablespace_map = VERIFY_RESULT(GetYsqlTablespaceInfo());

  // We clone the tablespace_manager and update the clone with the new tablespace_map that we
  // fetched above. We do this instead of updating the tablespace_manager object in-place because
  // other clients may have a shared_ptr to it through 'GetTablespaceManager()'.
  tablespace_manager = tablespace_manager->CreateCloneWithTablespaceMap(tablespace_map);
  {
    LockGuard lock(tablespace_mutex_);
    tablespace_manager_ = tablespace_manager;
  }

  return tablespace_manager->GetTablespaceReplicationInfo(tablespace_id);
}

Status CatalogManager::ValidateTableReplicationInfo(
    const ReplicationInfoPB& replication_info) const {
  if (!IsReplicationInfoSet(replication_info)) {
    return STATUS(InvalidArgument, "No replication info set.");
  }
  // We don't support read replica placements for now.
  if (!replication_info.read_replicas().empty()) {
    return STATUS(
        InvalidArgument,
        "Read replica placement info cannot be set for table level replication info.");
  }

  auto l = ClusterConfig()->LockForRead();
  const ReplicationInfoPB& cluster_replication_info = l->pb.replication_info();

  // If the replication info has placement_uuid set, verify that it matches the cluster
  // placement_uuid.
  if (replication_info.live_replicas().placement_uuid().empty()) {
    return Status::OK();
  }
  if (replication_info.live_replicas().placement_uuid() !=
      cluster_replication_info.live_replicas().placement_uuid()) {
    return STATUS(InvalidArgument, "Placement uuid for table level replication info "
        "must match that of the cluster's live placement info.");
  }
  return Status::OK();
}

Result<shared_ptr<TablespaceIdToReplicationInfoMap>> CatalogManager::GetYsqlTablespaceInfo() {
  auto table_info = GetTableInfo(
      VERIFY_RESULT(ysql_manager_->GetVersionSpecificCatalogTableId(kPgTablespaceTableId)));
  if (table_info == nullptr) {
    return STATUS(InternalError, "pg_tablespace table info not found");
  }

  auto tablespace_map = VERIFY_RESULT(sys_catalog_->ReadPgTablespaceInfo());

  // The tablespace options do not usually contain the placement uuid.
  // Populate the current cluster placement uuid into the placement information for
  // each tablespace.
  string placement_uuid;
  {
    auto cluster_config = ClusterConfig()->LockForRead();
    // TODO(deepthi.srinivasan): Read-replica placements are not supported as
    // of now.
    placement_uuid = cluster_config->pb.replication_info().live_replicas().placement_uuid();
  }

  if (!placement_uuid.empty()) {
    for (auto& iter : *tablespace_map) {
      if (iter.second) {
        iter.second.value().mutable_live_replicas()->set_placement_uuid(placement_uuid);
      }
    }
  }

  // Before updating the tablespace placement map, validate the
  // placement policies.
  for (auto& iter : *tablespace_map) {
    if (iter.second) {
      RETURN_NOT_OK(ValidateTableReplicationInfo(iter.second.value()));
    }
  }

  return tablespace_map;
}

boost::optional<TablespaceId> CatalogManager::GetTransactionStatusTableTablespace(
    const scoped_refptr<TableInfo>& table) {
  auto lock = table->LockForRead();
  if (lock->pb.table_type() != TRANSACTION_STATUS_TABLE_TYPE) {
    return boost::none;
  }

  if (!lock->pb.has_transaction_table_tablespace_id()) {
    return boost::none;
  }

  return lock->pb.transaction_table_tablespace_id();
}

void CatalogManager::ClearTransactionStatusTableTablespace(const scoped_refptr<TableInfo>& table) {
  auto lock = table->LockForWrite();
  if (lock->pb.table_type() != TRANSACTION_STATUS_TABLE_TYPE) {
    return;
  }

  lock.mutable_data()->pb.clear_transaction_table_tablespace_id();
  lock.mutable_data()->pb.set_version(lock.mutable_data()->pb.version() + 1);
  lock.Commit();
}

bool CatalogManager::CheckTransactionStatusTablesWithMissingTablespaces(
    const TablespaceIdToReplicationInfoMap& tablespace_info) {
  SharedLock lock(mutex_);
  for (const auto& table_id : transaction_table_ids_set_) {
    auto table = tables_->FindTableOrNull(table_id);
    if (table == nullptr) {
      LOG(DFATAL) << "Table uuid " << table_id
                  << " in transaction_table_ids_set_ but not in table_ids_map_";
      continue;
    }
    auto tablespace_id = GetTransactionStatusTableTablespace(table);
    if (tablespace_id && !tablespace_info.count(*tablespace_id)) {
      return true;
    }
  }
  return false;
}

Status CatalogManager::UpdateTransactionStatusTableTablespaces(
    const TablespaceIdToReplicationInfoMap& tablespace_info, const LeaderEpoch& epoch) {
  if (CheckTransactionStatusTablesWithMissingTablespaces(tablespace_info)) {
    LockGuard lock(mutex_);
    for (const auto& table_id : transaction_table_ids_set_) {
      auto table = tables_->FindTableOrNull(table_id);
      if (table == nullptr) {
        LOG(DFATAL) << "Table uuid " << table_id
                    << " in transaction_table_ids_set_ but not in table_ids_map_";
        continue;
      }
      auto tablespace_id = GetTransactionStatusTableTablespace(table);
      if (tablespace_id && !tablespace_info.count(*tablespace_id)) {
        LOG(INFO) << "Found transaction status table for tablespace id " << *tablespace_id
                  << " which doesn't exist, clearing tablespace id and deleting";
        ClearTransactionStatusTableTablespace(table);
        RETURN_NOT_OK(ScheduleDeleteTable(table, epoch));
      }
    }
  }
  return Status::OK();
}

Result<shared_ptr<TableToTablespaceIdMap>> CatalogManager::GetYsqlTableToTablespaceMap(
    const TablespaceIdToReplicationInfoMap& tablespace_info) {
  auto table_to_tablespace_map = std::make_shared<TableToTablespaceIdMap>();

  // First fetch all namespaces. This is because the table_to_tablespace information is only
  // found in the pg_class catalog table. There exists a separate pg_class table in each
  // namespace. To build in-memory state for all tables, process pg_class table for each
  // namespace.
  vector<NamespaceId> namespace_id_vec;
  set<NamespaceId> colocated_namespaces;
  {
    SharedLock lock(mutex_);
    for (const auto& ns : namespace_ids_map_) {
      if (ns.second->state() != SysNamespaceEntryPB::RUNNING) {
        continue;
      }

      if (ns.second->database_type() != YQL_DATABASE_PGSQL) {
        continue;
      }

      if (ns.first == kPgSequencesDataNamespaceId) {
        // Skip the database created for sequences system table.
        continue;
      }

      if (ns.second->colocated()) {
        colocated_namespaces.insert(ns.first);
      }

      // TODO (Deepthi): Investigate if safe to skip template0 and template1 as well.
      namespace_id_vec.emplace_back(ns.first);
    }

    // Add local transaction tables corresponding to tablespaces.
    for (const auto& table_id : transaction_table_ids_set_) {
      auto table = tables_->FindTableOrNull(table_id);
      if (table == nullptr) {
        LOG(DFATAL) << "Table uuid " << table_id
                    << " in transaction_table_ids_set_ but not in table_ids_map_";
        continue;
      }
      auto tablespace_id = GetTransactionStatusTableTablespace(table);
      if (tablespace_id) {
        if (tablespace_info.count(*tablespace_id)) {
          (*table_to_tablespace_map)[table->id()] = *tablespace_id;
        } else {
          // It's possible that a new tablespace had its transaction table created then deleted
          // between when we checked tablespace ids and now; we ignore it here, and it will be
          // caught and cleared in the next tablespace update.
          LOG(INFO) << "Found transaction status table for tablespace id " << *tablespace_id
                    << " which doesn't exist, ignoring";
        }
      }
    }
  }

  // For each namespace, fetch the table->tablespace information by reading pg_class
  // table for each namespace.
  for (const NamespaceId& nsid : namespace_id_vec) {
    VLOG(1) << "Refreshing placement information for namespace " << nsid;
    const uint32_t database_oid = CHECK_RESULT(GetPgsqlDatabaseOid(nsid));
    const bool is_colocated_database = colocated_namespaces.count(nsid) > 0;
    Status table_tablespace_status = sys_catalog_->ReadPgClassInfo(database_oid,
                                                                   is_colocated_database,
                                                                   table_to_tablespace_map.get());
    if (!table_tablespace_status.ok()) {
      LOG(WARNING) << "Refreshing table->tablespace info failed for namespace "
                   << nsid << " with error: " << table_tablespace_status.ToString();
    }

    const TableId tablegroup_table_id =
        VERIFY_RESULT(ysql_manager_->GetVersionSpecificCatalogTableId(
            GetPgsqlTableId(database_oid, kPgYbTablegroupTableOid)));
    const bool pg_yb_tablegroup_exists =
        VERIFY_RESULT(DoesTableExist(FindTableById(tablegroup_table_id)));

    // no pg_yb_tablegroup means we only need to check pg_class
    if (table_tablespace_status.ok() && !pg_yb_tablegroup_exists) {
      VLOG(5) << "Successfully refreshed placement information for namespace " << nsid
              << " from pg_class";
      continue;
    }

    Status tablegroup_tablespace_status = sys_catalog_->ReadTablespaceInfoFromPgYbTablegroup(
      database_oid,
      is_colocated_database,
      table_to_tablespace_map.get());
    if (!tablegroup_tablespace_status.ok()) {
      LOG(WARNING) << "Refreshing tablegroup->tablespace info failed for namespace "
                  << nsid << " with error: " << tablegroup_tablespace_status.ToString();
    }
    if (table_tablespace_status.ok() && tablegroup_tablespace_status.ok()) {
      VLOG(5) << "Successfully refreshed placement information for namespace " << nsid
              << " from pg_class and pg_yb_tablegroup";
    }
  }

  return table_to_tablespace_map;
}

Status CatalogManager::CreateTransactionStatusTablesForTablespaces(
    const TablespaceIdToReplicationInfoMap& tablespace_info,
    const TableToTablespaceIdMap& table_to_tablespace_map, const LeaderEpoch& epoch) {
  if (!GetAtomicFlag(&FLAGS_enable_ysql_tablespaces_for_placement) ||
      !GetAtomicFlag(&FLAGS_auto_create_local_transaction_tables)) {
    return Status::OK();
  }

  std::unordered_set<TablespaceId> valid_tablespaces;
  for (const auto& entry : table_to_tablespace_map) {
    if (entry.second) {
      valid_tablespaces.insert(*entry.second);
    }
  }
  for (const auto& entry : tablespace_info) {
    if (!entry.second) {
      valid_tablespaces.erase(entry.first);
    }
  }

  for (const auto& tablespace_id : valid_tablespaces) {
    RETURN_NOT_OK(
        CreateLocalTransactionStatusTableIfNeeded(nullptr /* rpc */, tablespace_id, epoch));
  }

  return Status::OK();
}

void CatalogManager::StartTablespaceBgTaskIfStopped() {
  if (GetAtomicFlag(&FLAGS_ysql_tablespace_info_refresh_secs) <= 0 ||
      !GetAtomicFlag(&FLAGS_enable_ysql_tablespaces_for_placement) ||
      GetAtomicFlag(&FLAGS_create_initial_sys_catalog_snapshot)) {
    // The tablespace bg task is disabled. Nothing to do.
    return;
  }

  const bool is_task_running = tablespace_bg_task_running_.exchange(true);
  if (is_task_running) {
    // Task already running, nothing to do.
    return;
  }

  ScheduleRefreshTablespaceInfoTask(true /* schedule_now */);
}

void CatalogManager::ScheduleRefreshTablespaceInfoTask(const bool schedule_now) {
  int wait_time = 0;

  if (!schedule_now) {
    wait_time = GetAtomicFlag(&FLAGS_ysql_tablespace_info_refresh_secs);
    if (wait_time <= 0) {
      // The tablespace refresh task has been disabled.
      tablespace_bg_task_running_ = false;
      return;
    }
  }

  refresh_ysql_tablespace_info_task_.Schedule([this](const Status& status) {
    Status s = background_tasks_thread_pool_->SubmitFunc(
      std::bind(&CatalogManager::RefreshTablespaceInfoPeriodically, this));
    if (!s.IsOk()) {
      // Failed to submit task to the thread pool. Mark that the task is now
      // no longer running.
      LOG(WARNING) << "Failed to schedule: RefreshTablespaceInfoPeriodically";
      tablespace_bg_task_running_ = false;
    }
  }, wait_time * 1s);
}

void CatalogManager::RefreshTablespaceInfoPeriodically() {
  if (!GetAtomicFlag(&FLAGS_enable_ysql_tablespaces_for_placement)) {
    tablespace_bg_task_running_ = false;
    return;
  }

  LeaderEpoch epoch;
  {
    SCOPED_LEADER_SHARED_LOCK(l, this);
    if (!l.IsInitializedAndIsLeader()) {
      LOG(INFO) << "No longer the leader, so cancelling tablespace info task";
      tablespace_bg_task_running_ = false;
      return;
    }
    epoch = l.epoch();
  }

  // Refresh the tablespace info in memory.
  Status s = DoRefreshTablespaceInfo(epoch);
  if (!s.IsOk()) {
    LOG(WARNING) << "Tablespace refresh task failed with error " << s.ToString();
  }

  // Schedule the next iteration of the task.
  ScheduleRefreshTablespaceInfoTask();
}

Status CatalogManager::DoRefreshTablespaceInfo(const LeaderEpoch& epoch) {
  VLOG(2) << "Running RefreshTablespaceInfoPeriodically task";

  shared_ptr<TablespaceIdToReplicationInfoMap> tablespace_info;
  {
    SCOPED_LEADER_SHARED_LOCK(l, this);
    RETURN_NOT_OK(l.first_failed_status());
    // First refresh the tablespace info in memory.
    tablespace_info = VERIFY_RESULT(GetYsqlTablespaceInfo());
  }

  // Clear tablespace ids for transaction tables mapped to missing tablespaces.
  RETURN_NOT_OK(UpdateTransactionStatusTableTablespaces(*tablespace_info, epoch));

  shared_ptr<TableToTablespaceIdMap> table_to_tablespace_map = nullptr;

  if (tablespace_info->size() > kYsqlNumDefaultTablespaces) {
    // There exist custom tablespaces in the system. Fetch the table->tablespace
    // map from PG catalog tables.
    table_to_tablespace_map = VERIFY_RESULT(GetYsqlTableToTablespaceMap(*tablespace_info));
  }

  // Update tablespace_manager_.
  {
    LockGuard lock(tablespace_mutex_);
    tablespace_manager_ = std::make_shared<YsqlTablespaceManager>(tablespace_info,
                                                                  table_to_tablespace_map);
  }

  if (table_to_tablespace_map) {
    // Trigger transaction table creates for tablespaces with tables and no transaction tables.
    RETURN_NOT_OK(CreateTransactionStatusTablesForTablespaces(
        *tablespace_info, *table_to_tablespace_map, epoch));
  }

  VLOG(3) << "Refreshed tablespace information in memory";
  return Status::OK();
}

Status CatalogManager::AddIndexInfoToTable(const scoped_refptr<TableInfo>& indexed_table,
                                           TableInfo::WriteLock* l_ptr,
                                           const IndexInfoPB& index_info,
                                           const LeaderEpoch& epoch,
                                           CreateTableResponsePB* resp) {
  LOG(INFO) << "AddIndexInfoToTable to " << indexed_table->ToString() << "  IndexInfo "
            << yb::ToString(index_info);
  auto& l = *l_ptr;
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(l, resp));

  // Make sure that the index appears to not have been added to the table until the tservers apply
  // the alter and respond back.
  // Heed issue #6233.
  if (!l->pb.has_fully_applied_schema()) {
    MultiStageAlterTable::CopySchemaDetailsToFullyApplied(&l.mutable_data()->pb);
  }

  // Add index info to indexed table and increment schema version.
  auto& pb = l.mutable_data()->pb;
  pb.add_indexes()->CopyFrom(index_info);
  pb.set_version(l.mutable_data()->pb.version() + 1);
  pb.set_updates_only_index_permissions(false);
  l.mutable_data()->set_state(
      SysTablesEntryPB::ALTERING,
      Format("Add index info version=$0 ts=$1", pb.version(), LocalTimeAsString()));

  // Update sys-catalog with the new indexed table info.
  TRACE("Updating indexed table metadata on disk");
  RETURN_NOT_OK(sys_catalog_->Upsert(epoch, indexed_table));

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l.Commit();

  RETURN_NOT_OK(SendAlterTableRequest(indexed_table, epoch));

  return Status::OK();
}

template <class Req, class Resp, class Action>
Status CatalogManager::PerformOnSysCatalogTablet(const Req& req, Resp* resp, const Action& action) {
  auto tablet_peer = sys_catalog_->tablet_peer();
  auto tablet = tablet_peer ? tablet_peer->shared_tablet() : nullptr;
  if (!tablet) {
    return SetupError(
        resp->mutable_error(),
        MasterErrorPB::TABLET_NOT_RUNNING,
        STATUS(NotFound, "The sys catalog tablet was not found."));
  }

  auto s = action(tablet);
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, s);
  }

  return Status::OK();
}

Status CatalogManager::FlushSysCatalog(
    const FlushSysCatalogRequestPB* req,
    FlushSysCatalogResponsePB* resp,
    rpc::RpcContext* context) {
  return PerformOnSysCatalogTablet(req, resp, [](auto shared_tablet) {
    return shared_tablet->Flush(tablet::FlushMode::kSync);
  });
}

Status CatalogManager::CompactSysCatalog(
    const CompactSysCatalogRequestPB* req,
    CompactSysCatalogResponsePB* resp,
    rpc::RpcContext* context) {
  return PerformOnSysCatalogTablet(req, resp, [&](auto shared_tablet) {
    return shared_tablet->ForceManualRocksDBCompact();
  });
}

namespace {

Result<std::array<PartitionPB, kNumSplitParts>> CreateNewTabletsPartition(
    const TabletInfo& tablet_info, const std::string& split_partition_key) {
  // Making a copy of PartitionPB to avoid holding a lock.
  const auto source_partition = tablet_info.LockForRead()->pb.partition();

  if (split_partition_key <= source_partition.partition_key_start() ||
      (!source_partition.partition_key_end().empty() &&
       split_partition_key >= source_partition.partition_key_end())) {
    return STATUS_FORMAT(
        InvalidArgument,
        "Can't split tablet $0 (partition_key_start: $1 partition_key_end: $2) by partition "
        "boundary (split_key: $3)",
        tablet_info.tablet_id(),
        FormatBytesAsStr(source_partition.partition_key_start()),
        FormatBytesAsStr(source_partition.partition_key_end()),
        FormatBytesAsStr(split_partition_key));
  }

  std::array<PartitionPB, kNumSplitParts> new_tablets_partition;

  new_tablets_partition.fill(source_partition);

  new_tablets_partition[0].set_partition_key_end(split_partition_key);
  new_tablets_partition[1].set_partition_key_start(split_partition_key);
  static_assert(kNumSplitParts == 2, "We expect tablet to be split into 2 new tablets here");

  return new_tablets_partition;
}

}  // namespace

Status CatalogManager::TEST_SplitTablet(
    const TabletId& tablet_id, const std::string& split_encoded_key,
    const std::string& split_partition_key) {
  auto source_tablet_info = VERIFY_RESULT(GetTabletInfo(tablet_id));
  return DoSplitTablet(
      source_tablet_info, split_encoded_key, split_partition_key, ManualSplit::kTrue,
      GetLeaderEpochInternal());
}

Status CatalogManager::TEST_SplitTablet(
    const TabletInfoPtr& source_tablet_info, docdb::DocKeyHash split_hash_code) {
  return DoSplitTablet(
      source_tablet_info, split_hash_code, ManualSplit::kTrue, GetLeaderEpochInternal());
}

Status CatalogManager::TEST_IncrementTablePartitionListVersion(const TableId& table_id) {
  auto table_info = GetTableInfo(table_id);
  SCHECK(table_info != nullptr, NotFound, Format("Table $0 not found", table_id));

  LockGuard lock(mutex_);
  auto table_lock = table_info->LockForWrite();
  auto& table_pb = table_lock.mutable_data()->pb;
  table_pb.set_partition_list_version(table_pb.partition_list_version() + 1);
  RETURN_NOT_OK(sys_catalog_->Upsert(GetLeaderEpochInternal(), table_info));
  table_lock.Commit();
  return Status::OK();
}

Status CatalogManager::ShouldSplitValidCandidate(
    const TabletInfo& tablet_info, const TabletReplicaDriveInfo& drive_info) const {
  if (drive_info.may_have_orphaned_post_split_data) {
    return STATUS_FORMAT(IllegalState, "Tablet $0 may have uncompacted post-split data.",
        tablet_info.id());
  }
  ssize_t size = drive_info.sst_files_size;
  DCHECK(size >= 0) << "Detected overflow in casting sst_files_size to signed int.";
  if (size < FLAGS_tablet_split_low_phase_size_threshold_bytes) {
    return STATUS_FORMAT(IllegalState, "Tablet $0 SST size ($0) < low phase size threshold ($1).",
        tablet_info.id(), size, FLAGS_tablet_split_low_phase_size_threshold_bytes);
  }
  TSDescriptorVector ts_descs = GetAllLiveNotBlacklistedTServers();

  size_t num_servers = 0;
  auto table_replication_info = CatalogManagerUtil::GetTableReplicationInfo(
      tablet_info.table(),
      GetTablespaceManager(),
      ClusterConfig()->LockForRead()->pb.replication_info());

  // If there is custom placement information present then
  // only count the tservers which the table has access to
  // according to the placement policy
  if (table_replication_info.has_live_replicas()) {
    auto pb = table_replication_info.live_replicas();
    auto valid_tservers_res =
        FindTServersForPlacementInfo(table_replication_info.live_replicas(), ts_descs);
    if (!valid_tservers_res.ok()) {
      num_servers = ts_descs.size();
    } else {
      num_servers = valid_tservers_res.get().size();
    }
  } else {
    num_servers = ts_descs.size();
  }

  if (num_servers == 0) {
    return STATUS_FORMAT(IllegalState,
                         "No live, non-blacklisted tservers for tablet $0. "
                         "Cannot calculate average number of tablets per tserver.",
                         tablet_info.id());
  }
  int64 num_tablets_per_server = tablet_info.table()->NumPartitions() / num_servers;

  if (num_tablets_per_server < FLAGS_tablet_split_low_phase_shard_count_per_node) {
    if (size <= FLAGS_tablet_split_low_phase_size_threshold_bytes) {
      return STATUS_FORMAT(IllegalState,
          "Table $0 num_tablets_per_server ($1) is less than "
          "tablet_split_low_phase_shard_count_per_node "
          "($2). Tablet $3 size ($4) <= tablet_split_low_phase_size_threshold_bytes ($5).",
          tablet_info.table()->id(), num_tablets_per_server,
          FLAGS_tablet_split_low_phase_shard_count_per_node, tablet_info.tablet_id(), size,
          FLAGS_tablet_split_low_phase_size_threshold_bytes);
    }
    return Status::OK();
  }
  if (num_tablets_per_server < FLAGS_tablet_split_high_phase_shard_count_per_node) {
    if (size <= FLAGS_tablet_split_high_phase_size_threshold_bytes) {
      return STATUS_FORMAT(IllegalState,
          "Table $0 num_tablets_per_server ($1) is less than "
          "tablet_split_high_phase_shard_count_per_node "
          "($2). Tablet $3 size ($4) <= tablet_split_high_phase_size_threshold_bytes ($5).",
          tablet_info.table()->id(), num_tablets_per_server,
          FLAGS_tablet_split_high_phase_shard_count_per_node, tablet_info.tablet_id(), size,
          FLAGS_tablet_split_high_phase_size_threshold_bytes);
    }
    return Status::OK();
  }
  if (size <= FLAGS_tablet_force_split_threshold_bytes) {
    return STATUS_FORMAT(IllegalState,
        "Tablet $0 size ($1) <= tablet_force_split_threshold_bytes ($2)",
        tablet_info.tablet_id(), size, FLAGS_tablet_force_split_threshold_bytes);
  }
  return Status::OK();
}

Status CatalogManager::DoSplitTablet(
    const TabletInfoPtr& source_tablet_info, std::string split_encoded_key,
    std::string split_partition_key, const ManualSplit is_manual_split, const LeaderEpoch& epoch) {
  std::vector<TabletInfoPtr> new_tablets;
  std::array<TabletId, kNumSplitParts> child_tablet_ids_sorted;
  auto table = source_tablet_info->table();

  // Note that the tablet map update is deferred to avoid holding the catalog manager mutex during
  // the sys catalog upsert.
  {
    // The validation functions below take read locks on the table and tablet. The write lock here
    // prevents the state from changing between the individual read locks. The locking order of
    // mutex_ -> table -> tablet is required to prevent deadlocks.
    // TODO: Pass the table and tablet locks in directly to the validation functions.
    SharedLock lock(mutex_);
    auto source_table_lock = table->LockForWrite();
    auto source_tablet_lock = source_tablet_info->LockForWrite();

    // We must re-validate the split candidate here *after* grabbing locks on the table and tablet.
    // If this is a manual split, then we should select all potential tablets for the split
    // (i.e. ignore the disabled tablets list and ignore TTL validation).
    auto s = ValidateSplitCandidateUnlocked(source_tablet_info, is_manual_split);
    if (!s.ok()) {
      VLOG_WITH_FUNC(4) << Format(
          "ValidateSplitCandidate for tablet $0 returned: $1.",
          source_tablet_info->tablet_id(), s);
      return s;
    }

    auto drive_info = VERIFY_RESULT(source_tablet_info->GetLeaderReplicaDriveInfo());
    if (!is_manual_split) {
      // It is possible that we queued up a split candidate in TabletSplitManager which was, at the
      // time, a valid split candidate, but by the time the candidate was actually processed here,
      // the cluster may have changed, putting us in a new split threshold phase, and it may no
      // longer be a valid candidate. This is not an unexpected error, but we should bail out of
      // splitting this tablet regardless.
      Status status = ShouldSplitValidCandidate(*source_tablet_info, drive_info);
      if (!status.ok()) {
        return STATUS_FORMAT(
            InvalidArgument,
            "Tablet split candidate $0 is no longer a valid split candidate: $1",
            source_tablet_info->tablet_id(),
            status);
      }
    }
    // After this point, we expect to split the tablet.

    // If child tablets are already registered, use the existing split key and tablets.
    if (source_tablet_lock->pb.split_tablet_ids().size() > 0) {
      const auto parent_partition = source_tablet_lock->pb.partition();
      for (auto& split_tablet_id : source_tablet_lock->pb.split_tablet_ids()) {
        // This should only fail if there is a concurrent split on the same tablet that has not yet
        // inserted the child tablets into the tablets map.
        auto existing_child = VERIFY_RESULT(GetTabletInfoUnlocked(split_tablet_id));
        const auto child_partition = existing_child->LockForRead()->pb.partition();

        if (parent_partition.partition_key_start() == child_partition.partition_key_start()) {
          child_tablet_ids_sorted[0] = existing_child->id();
          split_partition_key = child_partition.partition_key_end();
        } else {
          SCHECK_EQ(parent_partition.partition_key_end(), child_partition.partition_key_end(),
            IllegalState, "Parent partition key end does not equal child partition key end");
          child_tablet_ids_sorted[1] = existing_child->id();
          split_partition_key = child_partition.partition_key_start();
        }
      }
      // Re-compute the encoded key to ensure we use the same partition boundary for both child
      // tablets.
      split_encoded_key = VERIFY_RESULT(PartitionSchema::GetEncodedPartitionKey(
          split_partition_key, source_table_lock->pb.partition_schema()));
    }

    LOG(INFO) << "Starting tablet split: " << source_tablet_info->ToString()
              << " by partition key: " << Slice(split_partition_key).ToDebugHexString();

    // Get partitions for the split children.
    std::array<PartitionPB, kNumSplitParts> tablet_partitions = VERIFY_RESULT(
        CreateNewTabletsPartition(*source_tablet_info, split_partition_key));

    // Create in-memory (uncommitted) tablets for new split children.
    for (int i = 0; i < kNumSplitParts; ++i) {
      if (!child_tablet_ids_sorted[i].empty()) {
        continue;
      }
      auto new_child =
          CreateTabletInfo(table.get(), tablet_partitions[i], SysTabletsEntryPB::CREATING);
      child_tablet_ids_sorted[i] = new_child->id();
      new_tablets.push_back(std::move(new_child));
    }

    // Release the catalog manager mutex before doing disk IO to register new child tablets.
    lock.unlock();
    if (FLAGS_TEST_delay_split_registration_secs > 0) {
      SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_delay_split_registration_secs));
    }

    // Add new split children to the sys catalog.
    RETURN_NOT_OK(RegisterNewTabletsForSplit(
        source_tablet_info.get(), new_tablets, epoch, &source_table_lock, &source_tablet_lock));

    source_tablet_lock.Commit();
    source_table_lock.Commit();
  }
  // At this point, the tablets exist in the table but not in the tablet map. Users that need these
  // tablets should retry until the tablets are inserted into the tablet map.

  MAYBE_FAULT(FLAGS_TEST_fault_crash_after_registering_split_children);

  // Add the new tablets to the catalog manager tablet map.
  // TODO(16954): Can this be done atomically with the in-memory modification above by modifying
  // Upsert to add the tablets to the catalog manager map?
  if (!new_tablets.empty()) {
    LockGuard lock(mutex_);
    auto tablet_map_checkout = tablet_map_.CheckOut();
    for (const auto& new_tablet : new_tablets) {
      (*tablet_map_checkout)[new_tablet->id()] = std::move(new_tablet);
    }
  }

  // Handle xCluster metadata updates for local splits. Handle after registration but before
  // sending the split rpcs so that this is retried as part of the tablet split retry logic.
  SplitTabletIds split_tablet_ids {
    .source = source_tablet_info->tablet_id(),
    .children = { child_tablet_ids_sorted[0], child_tablet_ids_sorted[1] }
  };
  RETURN_NOT_OK(master_->tablet_split_manager().ProcessSplitTabletResult(
      source_tablet_info->table()->id(), split_tablet_ids, epoch));

  // TODO(tsplit): what if source tablet will be deleted before or during TS leader is processing
  // split? Add unit-test.
  RETURN_NOT_OK(SendSplitTabletRequest(
      source_tablet_info, child_tablet_ids_sorted, split_encoded_key, split_partition_key, epoch));

  return Status::OK();
}

Status CatalogManager::DoSplitTablet(
    const TabletInfoPtr& source_tablet_info, const docdb::DocKeyHash split_hash_code,
    const ManualSplit is_manual_split, const LeaderEpoch& epoch) {
  dockv::KeyBytes split_encoded_key;
  dockv::DocKeyEncoderAfterTableIdStep(&split_encoded_key)
      .Hash(split_hash_code, dockv::KeyEntryValues());
  const auto split_partition_key = PartitionSchema::EncodeMultiColumnHashValue(split_hash_code);
  return DoSplitTablet(
      source_tablet_info, split_encoded_key.ToStringBuffer(), split_partition_key, is_manual_split,
      epoch);
}

Result<TabletInfoPtr> CatalogManager::GetTabletInfo(const TabletId& tablet_id)
    EXCLUDES(mutex_) {
  SharedLock lock(mutex_);
  return GetTabletInfoUnlocked(tablet_id);
}

Result<TabletInfoPtr> CatalogManager::GetTabletInfoUnlocked(const TabletId& tablet_id)
    REQUIRES_SHARED(mutex_) {
  const auto tablet_info = FindPtrOrNull(*tablet_map_, tablet_id);
  SCHECK(tablet_info != nullptr, NotFound, Format("Tablet $0 not found", tablet_id));

  return tablet_info;
}

TabletInfos CatalogManager::GetTabletInfos(const std::vector<TabletId>& ids) {
  TabletInfos result;
  result.reserve(ids.size());
  SharedLock lock(mutex_);
  for (const auto& id : ids) {
    result.push_back(FindPtrOrNull(*tablet_map_, id));
  }
  return result;
}

void CatalogManager::SplitTabletWithKey(
    const TabletInfoPtr& tablet, const std::string& split_encoded_key,
    const std::string& split_partition_key, const ManualSplit is_manual_split,
    const LeaderEpoch& epoch) {
  // Note that DoSplitTablet() will trigger an async SplitTablet task, and will only return not OK()
  // if it failed to submit that task. In other words, any failures here are not retriable, and
  // success indicates that an async and automatically retrying task was submitted.
  auto s = DoSplitTablet(tablet, split_encoded_key, split_partition_key, is_manual_split, epoch);
  WARN_NOT_OK(
      s,
      Format("Failed to split tablet with GetSplitKey result for tablet: $0", tablet->tablet_id()));
}

Status CatalogManager::SplitTablet(
    const TabletId& tablet_id, const ManualSplit is_manual_split, const LeaderEpoch& epoch) {
  LOG(INFO) << "Got tablet to split: " << tablet_id << ", is manual split: " << is_manual_split;

  const auto tablet = VERIFY_RESULT(GetTabletInfo(tablet_id));
  return SplitTablet(tablet, is_manual_split, epoch);
}

Status CatalogManager::SplitTablet(
    const TabletInfoPtr& tablet, const ManualSplit is_manual_split,
    const LeaderEpoch& epoch) {
  VLOG(2) << "Scheduling GetSplitKey request to leader tserver for source tablet ID: "
          << tablet->tablet_id();
  auto call = std::make_shared<AsyncGetTabletSplitKey>(
      master_, AsyncTaskPool(), tablet, is_manual_split, epoch,
      [this, tablet, is_manual_split, epoch](const Result<AsyncGetTabletSplitKey::Data>& result) {
        if (result.ok()) {
          SplitTabletWithKey(
              tablet, result->split_encoded_key, result->split_partition_key, is_manual_split,
              epoch);
        } else if (
            tserver::TabletServerError(result.status()) ==
            tserver::TabletServerErrorPB::TABLET_SPLIT_DISABLED_TTL_EXPIRY) {
          LOG(INFO) << "AsyncGetTabletSplitKey task failed for tablet " << tablet->tablet_id()
                    << ". Tablet split not supported for tablets with TTL file expiration.";
          master_->tablet_split_manager().DisableSplittingForTtlTable(tablet->table()->id());
        } else if (
            tserver::TabletServerError(result.status()) ==
            tserver::TabletServerErrorPB::TABLET_SPLIT_KEY_RANGE_TOO_SMALL) {
          LOG(INFO) << "Tablet key range is too small to split, disabling splitting temporarily.";
          master_->tablet_split_manager().DisableSplittingForSmallKeyRangeTablet(tablet->id());
        } else {
          LOG(WARNING) << "AsyncGetTabletSplitKey task failed with status: " << result.status();
        }
      });
  tablet->table()->AddTask(call);
  return ScheduleTask(call);
}

Status CatalogManager::SplitTablet(
    const SplitTabletRequestPB* req, SplitTabletResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  const auto is_manual_split = ManualSplit::kTrue;
  const auto tablet = VERIFY_RESULT(GetTabletInfo(req->tablet_id()));

  RETURN_NOT_OK(ValidateSplitCandidate(tablet, is_manual_split));
  return SplitTablet(tablet, is_manual_split, epoch);
}

Status CatalogManager::XReplValidateSplitCandidateTable(const TableId& table_id) const {
  SharedLock lock(mutex_);
  return XReplValidateSplitCandidateTableUnlocked(table_id);
}

Status CatalogManager::XReplValidateSplitCandidateTableUnlocked(const TableId& table_id) const {
  RETURN_NOT_OK(xcluster_manager_->ValidateSplitCandidateTable(table_id));

  // Check if this table is part of a cdcsdk stream.
  if (!FLAGS_enable_tablet_split_of_cdcsdk_streamed_tables && IsTablePartOfCDCSDK(table_id)) {
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for tables that are a part of"
        " a CDCSDK stream, table_id: $0",
        table_id);
  }

  if (!FLAGS_enable_tablet_split_of_replication_slot_streamed_tables &&
      IsTablePartOfCDCSDK(table_id, true /* require_replication_slot */)) {
    return STATUS_FORMAT(
        NotSupported,
        "Tablet splitting is not supported for tables that are a part of a replication slot, "
        "table_id: $0",
        table_id);
  }
  return Status::OK();
}

Status CatalogManager::ValidateSplitCandidate(
    const TabletInfoPtr& tablet, const ManualSplit is_manual_split) {
  SharedLock lock(mutex_);
  return ValidateSplitCandidateUnlocked(tablet, is_manual_split);
}

Status CatalogManager::ValidateSplitCandidateUnlocked(
    const TabletInfoPtr& tablet, const ManualSplit is_manual_split) {
  const IgnoreDisabledList ignore_disabled_list { is_manual_split.get() };
  RETURN_NOT_OK(master_->tablet_split_manager().ValidateSplitCandidateTable(
      tablet->table(), ignore_disabled_list));
  RETURN_NOT_OK(XReplValidateSplitCandidateTableUnlocked(tablet->table()->id()));

  const IgnoreTtlValidation ignore_ttl_validation { is_manual_split.get() };

  const TabletId parent_id = tablet->LockForRead()->pb.split_parent_tablet_id();
  auto parent_result = GetTabletInfoUnlocked(parent_id);
  TabletInfoPtr parent = parent_result.ok() ? parent_result.get() : nullptr;
  return master_->tablet_split_manager().ValidateSplitCandidateTablet(
      *tablet, parent, ignore_ttl_validation, ignore_disabled_list);
}

std::string CatalogManager::DeletingTableData::ToString() const {
  return Format(
      "table: $0($1), $2", table_info->name(), table_info->id(), delete_retainer.ToString());
}

Status CatalogManager::DeleteNotServingTablet(
    const DeleteNotServingTabletRequestPB* req, DeleteNotServingTabletResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing DeleteNotServingTablet request " << req->ShortDebugString();
  const auto& tablet_id = req->tablet_id();
  const auto tablet_info = VERIFY_RESULT(GetTabletInfo(tablet_id));

  if (PREDICT_FALSE(FLAGS_TEST_reject_delete_not_serving_tablet_rpc)) {
    DEBUG_ONLY_TEST_SYNC_POINT("CatalogManager::DeleteNotServingTablet:Reject");
    return STATUS(
        InvalidArgument, "Rejecting due to FLAGS_TEST_reject_delete_not_serving_tablet_rpc");
  }

  const auto& table_info = tablet_info->table();

  RETURN_NOT_OK(CheckIfForbiddenToDeleteTabletOf(table_info));

  if (VERIFY_RESULT(IsTableUndergoingPitrRestore(*table_info))) {
    LOG(INFO) << "Rejecting delete request for tablet " << tablet_id << " since PITR restore"
              << " is ongoing for it";
    return STATUS(
        InvalidArgument, "Rejecting because the table is still undergoing a PITR restore");
  }

  RETURN_NOT_OK(CatalogManagerUtil::CheckIfCanDeleteSingleTablet(tablet_info));

  const auto delete_retainer = VERIFY_RESULT(GetDeleteRetainerInfoForTabletDrop(*tablet_info));

  return DeleteOrHideTabletsAndSendRequests(
      {tablet_info}, delete_retainer,
      "Not serving tablet deleted upon request at " + LocalTimeAsString(), epoch);
}

Status CatalogManager::DdlLog(
    const DdlLogRequestPB* req, DdlLogResponsePB* resp, rpc::RpcContext* rpc) {
  return sys_catalog_->FetchDdlLog(resp->mutable_entries());
}

namespace {

Status ValidateCreateTableSchema(const Schema& schema, CreateTableResponsePB* resp) {
  if (schema.num_key_columns() <= 0) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                      STATUS(InvalidArgument, "Must specify at least one key column"));
  }
  for (size_t i = 0; i < schema.num_key_columns(); i++) {
    if (!IsTypeAllowableInKey(schema.column(i).type_info())) {
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                        STATUS(InvalidArgument, "Invalid datatype for primary key column"));
    }
  }
  return Status::OK();
}

// Extract a colocation ID from request if explicitly passed, or generate a new valid one.
// Will error if requested ID is taken or invalid.
template<typename ContainsColocationIdFn>
Result<ColocationId> ConceiveColocationId(
    const CreateTableRequestPB& req, ContainsColocationIdFn contains_colocation_id) {
  if (req.has_colocation_id()) {
    if (req.colocation_id() < kFirstNormalColocationId) {
      return STATUS_FORMAT(
          InvalidArgument, "Colocation ID cannot be less than $0", kFirstNormalColocationId)
          .CloneAndAddErrorCode(MasterError(MasterErrorPB::INVALID_SCHEMA));
    }
    if (contains_colocation_id(req.colocation_id())) {
      return STATUS_FORMAT(
          InvalidArgument, "Colocation group already contains a table with colocation ID $0",
          req.colocation_id())
          .CloneAndAddErrorCode(MasterError(MasterErrorPB::INVALID_SCHEMA));
    }
    return req.colocation_id();
  }

  // Generate a random colocation ID unique within colocation group.
  ColocationId colocation_id = kColocationIdNotSet;
  if (FLAGS_TEST_sequential_colocation_ids) {
    colocation_id = 20000;
  }
  do {
    if (PREDICT_FALSE(FLAGS_TEST_sequential_colocation_ids)) {
      colocation_id++;
    } else {
      // See comment on kFirstNormalColocationId.
      colocation_id =
          RandomUniformInt<ColocationId>(kFirstNormalColocationId,
                                         std::numeric_limits<ColocationId>::max());
    }
  } while (contains_colocation_id(colocation_id));

  return colocation_id;
}

}  // namespace

struct CatalogManager::CreateYsqlSysTableData {
  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  Schema schema;
  PartitionSchema partition_schema;
  std::vector<Partition> partitions;
  TransactionMetadata txn;
  TableInfoPtr table;

  Status InitRequest(
      uint32_t database_oid, const TableId& table_id, const PersistentTableInfo& info) {
    req.set_name(info.pb.name());
    req.mutable_namespace_()->set_id(info.namespace_id());
    req.set_table_type(PGSQL_TABLE_TYPE);
    req.mutable_schema()->CopyFrom(info.schema());
    req.set_is_pg_catalog_table(true);
    req.set_table_id(table_id);

    if (IsIndex(info.pb)) {
      const uint32_t indexed_table_oid =
        VERIFY_RESULT(GetPgsqlTableOid(GetIndexedTableId(info.pb)));
      const TableId indexed_table_id = GetPgsqlTableId(database_oid, indexed_table_oid);

      // Set index_info.
      // Previously created INDEX wouldn't have the attribute index_info.
      if (info.pb.has_index_info()) {
        req.mutable_index_info()->CopyFrom(info.pb.index_info());
        req.mutable_index_info()->set_indexed_table_id(indexed_table_id);
      }

      // Set deprecated field for index_info.
      req.set_indexed_table_id(indexed_table_id);
      req.set_is_local_index(PROTO_GET_IS_LOCAL(info.pb));
      req.set_is_unique_index(PROTO_GET_IS_UNIQUE(info.pb));
    }

    return CompleteInit();
  }

  Status InitRequest(const CreateTableRequestPB& req_) {
    req = req_;
    return CompleteInit();
  }

  Status CompleteInit() {
    LOG(INFO) << "CreateYsqlSysTable: " << req.name();

    // Verify no hash partition schema is specified.
    if (req.partition_schema().has_hash_schema()) {
      return SetupError(resp.mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                        STATUS(InvalidArgument,
                               "PostgreSQL system catalog tables are non-partitioned"));
    }

    if (req.table_type() != TableType::PGSQL_TABLE_TYPE) {
      return SetupError(resp.mutable_error(), MasterErrorPB::INVALID_SCHEMA,
                        STATUS_FORMAT(
                            InvalidArgument,
                            "Expected table type to be PGSQL_TABLE_TYPE ($0), got $1 ($2)",
                            PGSQL_TABLE_TYPE,
                            TableType_Name(req.table_type())));

    }

    RETURN_NOT_OK(SchemaFromPB(req.schema(), &schema));
    // If the schema contains column ids, we are copying a Postgres table from one namespace to
    // another. Anyway, validate the schema.
    RETURN_NOT_OK(ValidateCreateTableSchema(schema, &resp));
    if (!schema.has_column_ids()) {
      schema.InitColumnIdsByDefault();
    }
    schema.mutable_table_properties()->set_is_ysql_catalog_table(true);

    // Create partition schema and one partition.
    RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));

    // Tables with a transaction should be rolled back if the transaction does not get committed.
    // Store this on the table persistent state until the transaction has been a verified success.
    if (req.has_transaction() && FLAGS_enable_transactional_ddl_gc) {
      txn = VERIFY_RESULT(TransactionMetadata::FromPB(req.transaction()));
      RSTATUS_DCHECK(!txn.status_tablet.empty(), Corruption, "Given incomplete Transaction");
    }

    return Status::OK();
  }
};

Status CatalogManager::CreateYsqlSysTableInMemory(
    const NamespaceInfo& ns, CreateYsqlSysTableData& data) {
  auto& table = data.table;

  // Create table info in memory.
  TRACE("Acquired catalog manager lock");

  // Verify that the table does not exist, or has been deleted.
  table = tables_->FindTableOrNull(data.req.table_id());
  if (table != nullptr && !table->is_deleted()) {
    Status s = STATUS_SUBSTITUTE(AlreadyPresent,
        "YSQL table '$0.$1' (ID: $2) already exists", ns.name(), table->name(), table->id());
    LOG(WARNING) << "Found table: " << table->ToStringWithState()
                 << ". Failed creating YSQL system table with error: "
                 << s.ToString() << " Request:\n" << data.req.ShortDebugString();
    // Technically, client already knows table ID, but we set it anyway for unified handling of
    // AlreadyPresent errors. See comment in CreateTable()
    data.resp.set_table_id(table->id());
    return SetupError(data.resp.mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT, s);
  }

  RETURN_NOT_OK(CreateTableInMemory(
      data.req, data.schema, data.partition_schema, ns.id(), ns.name(), data.partitions,
      /* colocated */ false, IsSystemObject::kTrue, /* index_info */ nullptr,
      /* tablets */ nullptr, &data.resp, &table));

  if (!data.txn.transaction_id.IsNil()) {
    *table->mutable_metadata()->mutable_dirty()->pb.mutable_transaction() = data.req.transaction();
  }

  return Status::OK();
}

Status CatalogManager::AddYsqlSysTableToSystemTablet(
    const TabletInfoPtr& sys_catalog_tablet, CreateYsqlSysTableData& data,
    TabletInfo::WriteLock& lock) {
  lock.mutable_data()->pb.add_table_ids(data.table->id());
  return data.table->AddTablet(sys_catalog_tablet);
}

Status CatalogManager::CompleteCreateYsqlSysTable(
    CreateYsqlSysTableData& data, const LeaderEpoch& epoch,
    tablet::ChangeMetadataRequestPB* change_meta_req, SysCatalogWriter* writer) {
  // Update the on-disk table state to "running".
  data.table->mutable_metadata()->mutable_dirty()->pb.set_state(SysTablesEntryPB::RUNNING);

  TEST_PAUSE_IF_FLAG(TEST_pause_before_upsert_ysql_sys_table);
  Status s;
  if (!writer) {
    s = sys_catalog_->Upsert(epoch, data.table);
  } else {
    // The generation fence around sys catalog writes is bypassed here, but we go through it
    // above. Ideally we'd go through the check here as well but it's checked higher up in the sys
    // catalog API. Skipping the check here is safe because we're creating a new table as
    // opposed to overwriting an existing table.
    s = writer->Mutate<false>(QLWriteRequestPB::QL_STMT_UPDATE, data.table);
  }
  if (PREDICT_FALSE(!s.ok())) {
    return AbortTableCreation(
        data.table.get(), {},
        s.CloneAndPrepend("An error occurred while inserting to sys-tablets: "), &data.resp);
  }
  TRACE("Wrote table to system table");

  // Commit the in-memory state.
  data.table->mutable_metadata()->CommitMutation();

  // Verify Transaction gets committed, which occurs after table create finishes.
  if (data.req.has_transaction() && PREDICT_TRUE(FLAGS_enable_transactional_ddl_gc)) {
    LOG(INFO) << "Enqueuing table for Transaction Verification: " << data.req.name();
    ScheduleVerifyTablePgLayer(data.txn, data.table, epoch);
  }

  tablet::ChangeMetadataRequestPB change_req;
  if (!change_meta_req) {
    change_meta_req = &change_req;
  }
  tablet::TableInfoPB* add_table;

  change_meta_req->set_tablet_id(kSysCatalogTabletId);

  if (writer) {
    add_table = change_meta_req->add_add_multiple_tables();
  } else {
    add_table = change_meta_req->mutable_add_table();
  }
  CatalogManagerUtil::FillTableInfoPB(
      data.req.table_id(), data.req.name(), TableType::PGSQL_TABLE_TYPE, data.schema,
      /* schema_version */ 0, data.partition_schema, add_table);

  // If not batched then perform change metadata operation now,
  // otherwise the caller is responsible for doing it.
  if (!writer) {
    RETURN_NOT_OK(tablet::SyncReplicateChangeMetadataOperation(
        change_meta_req, sys_catalog_->tablet_peer().get(), epoch.leader_term));
  }

  // No batching for serializing to initdb.
  if (initial_snapshot_writer_) {
    initial_snapshot_writer_->AddMetadataChange(*change_meta_req);
  }
  return Status::OK();
}

Status CatalogManager::CreateYsqlSysTable(
    const NamespaceInfo& ns, CreateYsqlSysTableData& data, const LeaderEpoch& epoch,
    tablet::ChangeMetadataRequestPB* change_meta_req, SysCatalogWriter* writer) {
  TabletInfoPtr sys_catalog_tablet;
  {
    LockGuard lock(mutex_);
    RETURN_NOT_OK(CreateYsqlSysTableInMemory(ns, data));
    sys_catalog_tablet = tablet_map_->find(kSysCatalogTabletId)->second;
  }
  {
    auto tablet_lock = sys_catalog_tablet->LockForWrite();
    auto s = AddYsqlSysTableToSystemTablet(sys_catalog_tablet, data, tablet_lock);
    if (s.ok()) {
      s = sys_catalog_->Upsert(epoch, sys_catalog_tablet);
    }
    if (PREDICT_FALSE(!s.ok())) {
      return AbortTableCreation(
          data.table.get(), {},
          s.CloneAndPrepend("An error occurred while inserting to sys-tablets: "), &data.resp);
    }
    // TODO(zdrudi): to handle the new format for the sys tablet, set the child table's parent table
    // id and add to the tablet's in-memory list of hosted table ids.
    tablet_lock.Commit();
  }
  TRACE("Inserted new table info into CatalogManager maps");

  return CompleteCreateYsqlSysTable(data, epoch, change_meta_req, writer);
}

Status CatalogManager::ReservePgsqlOids(const ReservePgsqlOidsRequestPB* req,
                                        ReservePgsqlOidsResponsePB* resp,
                                        rpc::RpcContext* rpc) {
  VLOG(1) << "ReservePgsqlOids request: " << req->ShortDebugString();

  // Lookup namespace
  scoped_refptr<NamespaceInfo> ns;
  {
    SharedLock lock(mutex_);
    ns = FindPtrOrNull(namespace_ids_map_, req->namespace_id());
  }
  if (!ns) {
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND,
                      STATUS(NotFound, "Namespace not found", req->namespace_id()));
  }

  // Reserve oids.
  auto l = ns->LockForWrite();

  uint32_t begin_oid = l->pb.next_pg_oid();
  if (begin_oid < req->next_oid()) {
    begin_oid = req->next_oid();
  }
  if (begin_oid == std::numeric_limits<uint32_t>::max()) {
    LOG(WARNING) << Format("No more object identifier is available for Postgres database $0 ($1)",
                           l->pb.name(), req->namespace_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR,
                      STATUS(InvalidArgument, "No more object identifier is available"));
  }

  uint32_t end_oid = begin_oid + req->count();
  if (end_oid < begin_oid) {
    end_oid = std::numeric_limits<uint32_t>::max(); // Handle wraparound.
  }

  resp->set_begin_oid(begin_oid);
  resp->set_end_oid(end_oid);
  l.mutable_data()->pb.set_next_pg_oid(end_oid);

  // Update the on-disk state.
  const Status s = sys_catalog_->Upsert(leader_ready_term(), ns);
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, s);
  }

  // Commit the in-memory state.
  l.Commit();

  VLOG(1) << "ReservePgsqlOids response: " << resp->ShortDebugString();

  return Status::OK();
}

Status CatalogManager::GetYsqlCatalogConfig(const GetYsqlCatalogConfigRequestPB* req,
                                            GetYsqlCatalogConfigResponsePB* resp,
                                            rpc::RpcContext* rpc) {
  VLOG(1) << "GetYsqlCatalogConfig request: " << req->ShortDebugString();
  if (PREDICT_FALSE(FLAGS_TEST_get_ysql_catalog_version_from_sys_catalog)) {
    uint64_t catalog_version;
    uint64_t last_breaking_version;
    RETURN_NOT_OK(GetYsqlCatalogVersion(&catalog_version, &last_breaking_version));
    resp->set_version(catalog_version);
    return Status::OK();
  }

  resp->set_version(ysql_manager_->GetYsqlCatalogVersion());
  return Status::OK();
}

Status CatalogManager::CopyPgsqlSysTables(const NamespaceInfo& ns,
                                          const std::vector<scoped_refptr<TableInfo>>& tables,
                                          const LeaderEpoch& epoch) {
  if (tables.empty()) {
    return Status::OK();
  }
  const uint32_t database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(ns.id()));
  vector<TableId> source_table_ids;
  vector<TableId> target_table_ids;
  tablet::ChangeMetadataRequestPB change_meta_req;
  // No batching for initdb or if flag is not set.
  auto batching = !initial_snapshot_writer_ && FLAGS_batch_ysql_system_tables_metadata;
  std::vector<CreateYsqlSysTableData> table_datas;
  if (batching) {
    table_datas.reserve(tables.size());
  }
  auto writer = batching ? sys_catalog_->NewWriter(epoch.leader_term) : nullptr;

  for (const auto& table : tables) {
    const uint32_t table_oid = VERIFY_RESULT(GetPgsqlTableOid(table->id()));
    const TableId table_id = GetPgsqlTableId(database_oid, table_oid);

    // Hold read lock until rows from the table are copied also.
    auto l = table->LockForRead();

    // Skip shared table.
    if (l->pb.is_pg_shared_table()) {
      continue;
    }

    CreateYsqlSysTableData table_data;
    if (batching) {
      table_datas.emplace_back();
    }
    auto& data = batching ? table_datas.back() : table_data;

    Status status = data.InitRequest(database_oid, table_id, l.data());
    if (status.ok() && !batching) {
      status = CreateYsqlSysTable(ns, data, epoch);
    }
    if (!status.ok()) {
      return status.CloneAndPrepend(Substitute(
          "Failure when creating PGSQL System Tables: $0",
          data.resp.error().ShortDebugString()));
    }

    source_table_ids.push_back(table->id());
    target_table_ids.push_back(table_id);
  }

  if (!table_datas.empty()) {
    TabletInfoPtr sys_catalog_tablet;
    {
      LockGuard lock(mutex_);
      for (auto& data : table_datas) {
        RETURN_NOT_OK(CreateYsqlSysTableInMemory(ns, data));
      }
      sys_catalog_tablet = tablet_map_->find(kSysCatalogTabletId)->second;
    }
    {
      auto tablet_lock = sys_catalog_tablet->LockForWrite();
      for (auto& data : table_datas) {
        RETURN_NOT_OK(AddYsqlSysTableToSystemTablet(sys_catalog_tablet, data, tablet_lock));
      }
      RETURN_NOT_OK(writer->Mutate<false>(
          QLWriteRequestPB::QL_STMT_UPDATE, sys_catalog_tablet));

      tablet_lock.Commit();
    }

    for (auto& data : table_datas) {
      RETURN_NOT_OK(CompleteCreateYsqlSysTable(data, epoch, &change_meta_req, writer.get()));
    }

    // Sync change metadata requests for the entire batch.
    RETURN_NOT_OK(tablet::SyncReplicateChangeMetadataOperation(
        &change_meta_req, sys_catalog_->tablet_peer().get(), epoch.leader_term));
    // Sync sys catalog table upserts for the entire batch.
    RETURN_NOT_OK(sys_catalog_->SyncWrite(writer.get()));
  }

  RETURN_NOT_OK(
      sys_catalog_->CopyPgsqlTables(source_table_ids, target_table_ids, epoch.leader_term));
  return Status::OK();
}

size_t CatalogManager::GetNumLiveTServersForPlacement(const PlacementId& placement_id) {
  auto blacklist = BlacklistSetFromPB();
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptorsInCluster(
      &ts_descs, placement_id, (blacklist.ok() ? *blacklist : BlacklistSet()));
  return ts_descs.size();
}

TSDescriptorVector CatalogManager::GetAllLiveNotBlacklistedTServers() const {
  TSDescriptorVector ts_descs;
  auto blacklist = BlacklistSetFromPB();
  master_->ts_manager()->GetAllLiveDescriptors(
      &ts_descs, blacklist.ok() ? *blacklist : BlacklistSet());
  return ts_descs;
}

namespace {

std::string GetStatefulServiceTableName(const StatefulServiceKind& service_kind) {
  return ToLowerCase(StatefulServiceKind_Name(service_kind)) + "_table";
}
}  // namespace

Status CatalogManager::CanAddPartitionsToTable(
    size_t desired_partitions, const PlacementInfoPB& placement_info) {
  TSDescriptorVector ts_descs;
  master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);
  auto max_tablets = FLAGS_max_create_tablets_per_ts * ts_descs.size();
  auto replicas_per_tablet = GetNumReplicasOrGlobalReplicationFactor(placement_info);
  if (replicas_per_tablet > 1 && max_tablets > 0 && desired_partitions > max_tablets) {
    std::string msg = Substitute("The requested number of tablets ($0) is over the permitted "
                                 "maximum ($1)", desired_partitions, max_tablets);
    return STATUS(InvalidArgument, msg);
  }
  return Status::OK();
}

Status CatalogManager::CanSupportAdditionalTabletsForTableCreation(
    int num_tablets, const ReplicationInfoPB& replication_info,
    const TSDescriptorVector& ts_descs) {
  // Don't check for tablet limits if we potentially don't have information from all the live
  // TServers.  To make sure we have the needed information, don't check for
  // FLAGS_initial_tserver_registration_duration_secs after a master leadership change.
  if (TimeSinceElectedLeader() <
      MonoDelta::FromSeconds(FLAGS_initial_tserver_registration_duration_secs)) {
    return Status::OK();
  }
  return CanCreateTabletReplicas(num_tablets, replication_info, GetAllLiveNotBlacklistedTServers());
}

Status CatalogManager::CanSupportAdditionalTablet(
    const TableInfoPtr& table, const ReplicationInfoPB& replication_info) const {
  return CanCreateTabletReplicas(1, replication_info, GetAllLiveNotBlacklistedTServers());
}

namespace {

Status PrintTableInfoForYsqlMajorVersionUpgrade(const scoped_refptr<TableInfo>& table,
                                                const SchemaPB& request_schema_pb) {
  if (FLAGS_hide_pg_catalog_table_creation_logs) {
    return Status::OK();
  }
  Schema request_schema;
  RETURN_NOT_OK(SchemaFromPB(request_schema_pb, &request_schema));
  auto existing_schema = VERIFY_RESULT(table->GetSchema());
  if (!request_schema.Equals(existing_schema)) {
    // During a ysql major catalog upgrade, with columns that have been dropped, the master's Schema
    // object doesn't keep a record. However, PostgreSQL does. To restore ordering properly,
    // pg_restore sends a dummy value for each dropped column in the CreateTable request (see
    // comments about dropped columns in dumpTableSchema in pg_dump.c). We ignore the dummy value
    // here, and later ignore the ALTER TABLE DROP COLUMN statement inside
    // CatalogManager::AlterTable() as well. Note that the ordering will already be compatible
    // because an equivalent set of drop column steps has already happened on the prior ysql major
    // version.
    //
    // Here we log the existing and request schemas for debugging purposes.
    SchemaPB existing_schema_pb;
    SchemaToPB(existing_schema, &existing_schema_pb);
    LOG(INFO) << "During ysql major catalog upgrade, CreateTable request schema: "
              << request_schema_pb.DebugString()
              << " does not equal existing schema: " << existing_schema_pb.DebugString();
  }
  LOG(INFO) << "Table already exists with id " << table->id()
            << " during ysql major catalog upgrade, returning early from CreateTable";
  return Status::OK();
}

}  // namespace

Result<ColocationId> CatalogManager::ObtainColocationId(
    const CreateTableRequestPB& req, const TablegroupInfo* tablegroup,
    bool is_colocated_via_database, const NamespaceId& namespace_id,
    const NamespaceName& namespace_name, const TableInfoPtr& indexed_table) {
  if (tablegroup) {
    return ConceiveColocationId(req, [tablegroup](auto colocation_id) {
      return tablegroup->HasChildTable(colocation_id);
    });
  }

  std::vector<TableId> table_ids;
  if (is_colocated_via_database) {
    auto it = colocated_db_tablets_map_.find(namespace_id);
    if (it == colocated_db_tablets_map_.end()) {
      return STATUS_FORMAT(
          IllegalState, "Database $0 doesn't have a colocation tablet", namespace_name);
    }
    table_ids = it->second->GetTableIds();
  } else if (req.index_info().has_vector_idx_options()) {
    auto indexes = indexed_table->GetIndexInfos();
    table_ids.reserve(indexes.size());
    for (const auto& index : indexes) {
      table_ids.push_back(index.table_id());
    }
  } else {
    return STATUS(RuntimeError, "Unexpected colocation mode");
  }

  std::unordered_set<ColocationId> colocation_ids;
  colocation_ids.reserve(table_ids.size());
  for (const TableId& table_id : table_ids) {
    DCHECK(!table_id.empty());
    const auto colocated_table_info = GetTableInfoUnlocked(table_id);
    if (!colocated_table_info) {
      // Needed because of #11129, should be replaced with DCHECK after the fix.
      continue;
    }
    colocation_ids.insert(colocated_table_info->GetColocationId());
  }

  return ConceiveColocationId(req, [&colocation_ids](auto colocation_id) {
    return ContainsKey(colocation_ids, colocation_id);
  });
}

// Create a new table.
// See README file in this directory for a description of the design.
Status CatalogManager::CreateTable(const CreateTableRequestPB* orig_req,
                                   CreateTableResponsePB* resp,
                                   rpc::RpcContext* rpc,
                                   const LeaderEpoch& epoch) {
  DVLOG(3) << __PRETTY_FUNCTION__ << " Begin. " << orig_req->DebugString();

  const bool is_pg_table = orig_req->table_type() == PGSQL_TABLE_TYPE;
  const bool is_pg_catalog_table = is_pg_table && orig_req->is_pg_catalog_table();
  if (!is_pg_catalog_table || !FLAGS_hide_pg_catalog_table_creation_logs) {
    LOG(INFO) << "CreateTable from " << RequestorString(rpc)
                << ":\n" << orig_req->DebugString();
  } else {
    LOG(INFO) << "CreateTable from " << RequestorString(rpc) << ": " << orig_req->name();
  }

  // Look up the namespace and verify if it exists.
  TRACE("Looking up namespace");
  auto ns = VERIFY_RESULT(FindNamespace(orig_req->namespace_()));

  // If we're doing a ysql major catalog upgrade, then for a user table, we expect it to already
  // exist. We will wire the current version's catalog to it. For a PG catalog table, we expect it
  // to be creating the new version's PG catalog, and so the table must not already exist.
  if (IsYsqlMajorCatalogUpgradeInProgress()) {
    LockGuard lock(mutex_);
    auto ns_lock = ns->LockForRead();
    TRACE("Acquired catalog manager lock");

    RSTATUS_DCHECK_EQ(
        orig_req->table_type(), PGSQL_TABLE_TYPE, IllegalState,
        "Creating non-PGSQL table during a ysql major catalog upgrade");
    scoped_refptr<TableInfo> table = tables_->FindTableOrNull(orig_req->table_id());
    if (table != nullptr && !table->is_deleted()) {
      RSTATUS_DCHECK(!is_pg_catalog_table, IllegalState,
                     "Detected a pre-existing catalog table in CreateTable during a ysql major "
                     "version upgrade.");
      RETURN_NOT_OK(PrintTableInfoForYsqlMajorVersionUpgrade(table, orig_req->schema()));
      resp->set_table_id(table->id());
      return Status::OK();
    }
    RSTATUS_DCHECK(
        is_pg_catalog_table, IllegalState,
        "Trying to create a new user table during a ysql major catalog upgrade");
  }

  RETURN_NOT_OK(CreateGlobalTransactionStatusTableIfNeededForNewTable(*orig_req, rpc, epoch));
  RETURN_NOT_OK(MaybeCreateLocalTransactionTable(*orig_req, rpc, epoch));

  if (is_pg_catalog_table) {
    // No batching for migration.
    auto ns = VERIFY_RESULT(FindNamespace(orig_req->namespace_()));
    CreateYsqlSysTableData data;
    auto result = data.InitRequest(*orig_req);
    if (result.ok()) {
      result = CreateYsqlSysTable(*ns, data, epoch);
    }
    *resp = data.resp;
    return result;
  }

  if (!orig_req->old_rewrite_table_id().empty()) {
    auto table_id = orig_req->old_rewrite_table_id();
    auto namespace_id = VERIFY_RESULT(GetTableById(table_id))->LockForRead()->namespace_id();
    auto automatic_ddl_mode = xcluster_manager_->IsNamespaceInAutomaticDDLMode(namespace_id);
    SharedLock lock(mutex_);
    // Fail rewrites on tables that are part of CDC or non-automatic mode XCluster replication,
    // except for TRUNCATEs on CDC tables when FLAGS_enable_truncate_cdcsdk_table is enabled.
    if ((xcluster_manager_->IsTableReplicated(table_id) && !automatic_ddl_mode)  ||
        (IsTablePartOfCDCSDK(table_id) &&
         (!orig_req->is_truncate() || !FLAGS_enable_truncate_cdcsdk_table))) {
      return STATUS(
          NotSupported,
          "cannot rewrite a table that is a part of CDC or non-automatic mode XCluster replication"
          " See https://github.com/yugabyte/yugabyte-db/issues/16625.");
    }
  }

  // Copy the request, so we can fill in some defaults.
  CreateTableRequestPB req = *orig_req;

  // For index table, find the table info
  TableInfoPtr indexed_table;
  TableInfo::WriteLock indexed_table_write_lock;
  if (IsIndex(req)) {
    TRACE("Looking up indexed table");
    indexed_table = GetTableInfo(req.indexed_table_id());
    if (indexed_table == nullptr) {
      return STATUS_SUBSTITUTE(
            NotFound, "The indexed table $0 does not exist", req.indexed_table_id());
    }

    TRACE("Locking indexed table");
    RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(
        indexed_table->LockForRead(), resp));
  }

  // Validate schema.
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(req.schema(), &schema));
  RETURN_NOT_OK(ValidateCreateTableSchema(schema, resp));

  // Pre-colocation GA colocated tables in a legacy colocated database are colocated via database,
  // but after GA, colocated tables are colocated via tablegroups.
  bool is_colocated_via_database;
  NamespaceId namespace_id;
  NamespaceName namespace_name;
  {
    auto ns_lock = ns->LockForRead();
    if (ns->database_type() != GetDatabaseTypeForTable(req.table_type())) {
      Status s = STATUS(NotFound, "Namespace not found");
      return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
    }
    namespace_id = ns->id();
    namespace_name = ns->name();
    SharedLock lock(mutex_);
    is_colocated_via_database =
        (IsColocatedDbParentTableId(req.table_id()) ||
             colocated_db_tablets_map_.contains(namespace_id)) &&
        // Opt out of colocation if the request says so.
        (!req.has_is_colocated_via_database() || req.is_colocated_via_database()) &&
        // Opt out of colocation if the indexed table opted out of colocation.
        (!indexed_table || indexed_table->colocated());
  }

  const bool is_vector_index = req.index_info().has_vector_idx_options();
  const bool colocated =
      (is_colocated_via_database || req.has_tablegroup_id() || is_vector_index) &&
      // Any tables created in the xCluster DDL replication extension should not be colocated.
      schema.SchemaName() != xcluster::kDDLQueuePgSchemaName;
  SCHECK(!colocated || req.has_table_id(),
         InvalidArgument, "Colocated table should specify a table ID");

  // If ysql_enable_colocated_tables_with_tablespaces is not enabled then tablespaces cannot be
  // specified for indexes on colocated tables.
  SCHECK(
      FLAGS_ysql_enable_colocated_tables_with_tablespaces || !colocated || !IsIndex(req) ||
          !req.has_tablespace_id(),
      InvalidArgument, "TABLESPACE is not supported for indexes on colocated tables.");

  // TODO: If this is a colocated index table, convert any hash partition columns into
  // range partition columns.
  // This is because postgres does not know that this index table is in a colocated database.
  // When we get to the "tablespaces" step where we store this into PG metadata, then PG will know
  // if db/table is colocated and do the work there.
  if (colocated && IsIndex(req)) {
    for (auto& col_pb : *req.mutable_schema()->mutable_columns()) {
      col_pb.set_is_hash_key(false);
    }
  }

  // checking that referenced user-defined types (if any) exist.
  {
    SharedLock lock(mutex_);
    for (size_t i = 0; i < schema.num_columns(); i++) {
      for (const auto &udt_id : schema.column(i).type()->GetUserDefinedTypeIds()) {
        if (FindPtrOrNull(udtype_ids_map_, udt_id) == nullptr) {
          Status s = STATUS(InvalidArgument, "Referenced user-defined type not found");
          return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
        }
      }
    }
  }
  // TODO (ENG-1860) The referenced namespace and types retrieved/checked above could be deleted
  // some time between this point and table creation below.

  // Usually the column ids are available if it's called on the backup-restoring code path
  // (from CatalogManager::RecreateTable). Else the column ids must be empty in the client schema.
  if (!schema.has_column_ids()) {
    schema.InitColumnIdsByDefault();
  }

  if (colocated) {
    // If the table is colocated, then there should be no hash partition columns.
    // Do the same for tables that are being placed in tablegroups.
    if (schema.num_hash_key_columns() > 0) {
      Status s = STATUS(InvalidArgument, "Cannot colocate hash partitioned table");
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
    }
  } else if (
      !req.partition_schema().has_hash_schema() && !req.partition_schema().has_range_schema()) {
    // If neither hash nor range schema have been specified by the protobuf request, we assume the
    // table uses a hash schema, and we use the table_type and hash_key to determine the hashing
    // scheme (redis or multi-column) that should be used.
    if (req.table_type() == REDIS_TABLE_TYPE) {
      req.mutable_partition_schema()->set_hash_schema(PartitionSchemaPB::REDIS_HASH_SCHEMA);
    } else if (schema.num_hash_key_columns() > 0) {
      req.mutable_partition_schema()->set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
    } else {
      Status s = STATUS(InvalidArgument, "Unknown table type or partitioning method");
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
    }
  }

  if (IsReplicationInfoSet(req.replication_info()) && req.table_type() == PGSQL_TABLE_TYPE) {
    const Status s = STATUS(InvalidArgument, "Cannot set placement policy for YSQL tables "
        "use Tablespaces instead");
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  // Get placement info.
  const ReplicationInfoPB& replication_info = VERIFY_RESULT(
    GetTableReplicationInfo(req.replication_info(), req.tablespace_id()));
  // Whether the table is joining an existing colocation group, in other words it is a colocated
  // non-parent table. Such tables will reuse tablets of their respective colocation group.
  bool joining_colocation_group =
      colocated && !IsColocationParentTableId(req.table_id());

  int num_tablets = VERIFY_RESULT(
      CalculateNumTabletsForTableCreation(req, schema, replication_info.live_replicas()));
  auto s = CanAddPartitionsToTable(num_tablets, replication_info.live_replicas());
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::TOO_MANY_TABLETS, s);
  }
  if (!joining_colocation_group) {
    s = CanSupportAdditionalTabletsForTableCreation(
        num_tablets, replication_info, GetAllLiveNotBlacklistedTServers());
    if (!s.ok()) {
      IncrementCounter(metric_create_table_too_many_tablets_);
      return SetupError(resp->mutable_error(), MasterErrorPB::TOO_MANY_TABLETS, s);
    }
  }
  const auto [partition_schema, partitions] =
      VERIFY_RESULT(CreatePartitions(schema, num_tablets, colocated, &req, resp));

  if (!FLAGS_TEST_skip_placement_validation_createtable_api) {
    ValidateReplicationInfoRequestPB validate_req;
    validate_req.mutable_replication_info()->CopyFrom(replication_info);
    ValidateReplicationInfoResponsePB validate_resp;
    RETURN_NOT_OK(ValidateReplicationInfo(&validate_req, &validate_resp));
  }

  // For index table, populate the index info.
  IndexInfoPB index_info;

  const bool index_backfill_enabled = IsIndexBackfillEnabled(
      orig_req->table_type(), orig_req->schema().table_properties().is_transactional());
  if (req.has_index_info()) {
    // Current message format.
    index_info.CopyFrom(req.index_info());

    // Assign column-ids that have just been computed and assigned to "index_info".
    if (!is_pg_table) {
      DCHECK_EQ(index_info.columns().size(), schema.num_columns())
        << "Number of columns are not the same between index_info and index_schema";
      for (size_t colidx = 0; colidx < schema.num_columns(); colidx++) {
        index_info.mutable_columns(narrow_cast<int>(colidx))->set_column_id(
            schema.column_id(colidx));
      }
    }
  } else if (req.has_indexed_table_id()) {
    // Old client message format when rolling upgrade (Not having "index_info").
    IndexInfoBuilder index_info_builder(&index_info);
    index_info_builder.ApplyProperties(req.indexed_table_id(),
        req.is_local_index(), req.is_unique_index());
    if (orig_req->table_type() != PGSQL_TABLE_TYPE) {
      auto indexed_schema = VERIFY_RESULT(indexed_table->GetSchema());
      RETURN_NOT_OK(index_info_builder.ApplyColumnMapping(indexed_schema, schema));
    }
  }

  if ((req.has_index_info() || req.has_indexed_table_id()) &&
      index_backfill_enabled &&
      !req.skip_index_backfill()) {
    // Start off the index table with major compactions disabled. We need this to retain the delete
    // markers until the backfill process is completed.  No need to set index_permissions in the
    // index table.
    schema.SetRetainDeleteMarkers(true);
  }

  LOG(INFO) << "CreateTable with IndexInfo " << AsString(index_info);

  TableInfoPtr table;
  TabletInfos tablets;
  {
    UniqueLock lock(mutex_);
    auto ns_lock = ns->LockForRead();
    TRACE("Acquired catalog manager lock");

    // Verify that the table does not exist.
    // Use table_names_map_ to check for table existence for all table types except YSQL
    // tables.
    // Use tables_ to check for YSQL table existence because
    // (1) table_names_map_ isn't used for YSQL tables.
    // (2) under certain circumstance, a client can send out duplicate CREATE TABLE
    // requests containing same table id to master.
    // (3) two concurrent CREATE TABLEs using the same fully qualified name cannot both succeed
    // because of the pg_class unique index on the qualified name.
    Status s = Status::OK();
    if (req.table_type() == PGSQL_TABLE_TYPE) {
      table = tables_->FindTableOrNull(req.table_id());
      // We rarely remove deleted entries from tables_, so it is necessary to check if TableInfoPtr
      // retrieved from tables_ corresponds to a deleted table/index or not. If it corresponds to a
      // deleted entry, then the entry in tables_ can be replaced.
      // On the other hand, on master restart or leader change, we do not load deleted
      // tables/indexes in memory.
      if (table != nullptr && !table->is_deleted()) {
        s = STATUS_SUBSTITUTE(AlreadyPresent, "Object with id $0 ('$1.$2') already exists",
                              table->id(), ns->name(), table->name());
      }
    } else {
      table = FindPtrOrNull(table_names_map_, {namespace_id, req.name()});
      if (table != nullptr) {
        s = STATUS_SUBSTITUTE(AlreadyPresent,
                "Object '$0.$1' already exists", ns->name(), table->name());
      }
    }
    if (!s.ok()) {
      LOG(WARNING) << "Found table: " << table->ToStringWithState()
                   << ". Failed creating table with error: "
                   << s.ToString() << " Request:\n" << orig_req->DebugString();
      // If the table already exists, we set the response table_id field to the id of the table that
      // already exists. This is necessary because before we return the error to the client (or
      // success in case of a "CREATE TABLE IF NOT EXISTS" request) we want to wait for the existing
      // table to be available to receive requests. And we need the table id for that.
      resp->set_table_id(table->id());
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT, s);
    }

    // Namespace state validity check:
    // 1. Allow Namespaces that are RUNNING
    // 2. Allow Namespaces that are PREPARING under 2 situations
    //    2a. System Namespaces.
    //    2b. The parent table from a legacy Colocated Namespace.
    const auto parent_table_name = GetColocatedDbParentTableName(ns->id());
    bool valid_ns_state = (ns->state() == SysNamespaceEntryPB::RUNNING) ||
      (ns->state() == SysNamespaceEntryPB::PREPARING &&
        (ns->name() == kSystemNamespaceName || req.name() == parent_table_name));
    if (!valid_ns_state) {
      Status s = STATUS_SUBSTITUTE(TryAgain, "Invalid Namespace State ($0). Cannot create $1.$2",
          SysNamespaceEntryPB::State_Name(ns->state()), ns->name(), req.name());
      return SetupError(resp->mutable_error(), NamespaceMasterError(ns->state()), s);
    }

    // Check whether this CREATE TABLE request which has a tablegroup_id is for a normal user table
    // or the request to create the parent table for the tablegroup.
    TablegroupInfo* tablegroup = nullptr;
    if (colocated && req.has_tablegroup_id()) {
      tablegroup = tablegroup_manager_->Find(req.tablegroup_id());
      bool is_parent = IsTablegroupParentTableId(req.table_id());
      if (tablegroup == nullptr && !is_parent) {
        Status s = STATUS_SUBSTITUTE(InvalidArgument, "Tablegroup with ID $0 does not exist",
                                     req.tablegroup_id());
        return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
      }
      if (tablegroup != nullptr && is_parent) {
        Status s = STATUS_SUBSTITUTE(AlreadyPresent, "Tablegroup with ID $0 already exists",
                                     req.tablegroup_id());

        return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT, s);
      }
    }

    // Generate colocation ID in advance in order to fail before CreateTableInMemory is called.
    auto colocation_id = kColocationIdNotSet;
    if (joining_colocation_group) {
      auto result = ObtainColocationId(
          req, tablegroup, is_colocated_via_database, namespace_id, namespace_name, indexed_table);
      if (!result.ok()) {
        return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, result.status());
      }
      colocation_id = *result;
    }

    RETURN_NOT_OK(CreateTableInMemory(
        req, schema, partition_schema, namespace_id, namespace_name, partitions, colocated,
        IsSystemObject::kFalse, &index_info, joining_colocation_group ? nullptr : &tablets, resp,
        &table));

    // Section is executed when a table is either the parent table or a user table in a colocation
    // group.
    if (colocated) {
      if (!joining_colocation_group) {
        // This is a dummy parent table of a newly created colocation group.
        // It creates a dummy tablet for the colocation group along with updating the
        // catalog manager maps.
        tablets[0]->mutable_metadata()->mutable_dirty()->pb.set_colocated(true);
        if (is_colocated_via_database) {
          RSTATUS_DCHECK(
              table->IsColocatedDbParentTable(), InternalError,
              "Expected colocated database to produce a colocation parent table");
          RSTATUS_DCHECK_EQ(
              tablets.size(), 1U, InternalError,
              "Only one tablet should be created for each colocated database");
          CHECK(table->id() == GetColocatedDbParentTableId(namespace_id))
              << table->id() << ", " << namespace_id;

          colocated_db_tablets_map_[ns->id()] = tablet_map_->find(tablets[0]->id())->second;
        } else {
          RSTATUS_DCHECK(
              table->IsTablegroupParentTable(), InternalError,
              "Expected tablegroup to produce a tablegroup parent table");
          RSTATUS_DCHECK_EQ(
              tablets.size(), 1U, InternalError,
              "Only one tablet should be created for each tablegroup");
          CHECK(table->id() == GetTablegroupParentTableId(req.tablegroup_id()) ||
                table->id() == GetColocationParentTableId(req.tablegroup_id()))
              << table->id() << ", " << req.tablegroup_id() << ", " << namespace_id;

          // Define a new tablegroup
          DCHECK(tablegroup == nullptr);
          tablegroup =
              VERIFY_RESULT(tablegroup_manager_->Add(
                  ns->id(), req.tablegroup_id(),
                  tablet_map_->find(tablets[0]->id())->second));
        }
      } else {
        // Adding a table to an existing colocation tablet.
        if (is_vector_index) {
          tablets = VERIFY_RESULT(indexed_table->GetTablets());
        } else {
          auto tablet = tablegroup ?
              tablegroup->tablet() :
              colocated_db_tablets_map_[ns->id()];
          RSTATUS_DCHECK(
              tablet->colocated(), InternalError,
              Format("Colocation group tablet $0 should be marked as colocated",
                     tablet->id()));
          tablets.push_back(tablet);
        }
        lock.unlock();

        // If the request is to create a colocated index, need to aquired the write lock on the
        // indexed table before acquiring the write lock on the colocated tablet below to prevent
        // deadlock because a colocated index and its colocated indexed table share the same
        // colocated tablet.
        if (IsIndex(req)) {
          TRACE("Locking indexed table");
          indexed_table_write_lock = indexed_table->LockForWrite();
        }

        auto& table_pb = table->mutable_metadata()->mutable_dirty()->pb;
        for (auto& tablet : tablets) {
          tablet->mutable_metadata()->StartMutation();
          auto& tablet_pb = tablet->mutable_metadata()->mutable_dirty()->pb;
          if (table_pb.parent_table_id().empty()) {
            table_pb.set_parent_table_id(tablet_pb.table_id());
          } else {
            RSTATUS_DCHECK_EQ(table_pb.parent_table_id(), tablet_pb.table_id(), RuntimeError,
                              "Different table ids in tablets");
          }
        }

        CHECK_NE(colocation_id, kColocationIdNotSet);
        table_pb.mutable_schema()->mutable_colocated_table_id()->set_colocation_id(colocation_id);

        // TODO(zdrudi): In principle if the hosted_tables_mapped_by_parent_id field is set we could
        // avoid writing the tablets and even avoid any tablet mutations here at all. However
        // table->AddTablet assumes the tablet has a write in progress and checkfails if it doesn't.
        for (const auto& tablet : tablets) {
          RETURN_NOT_OK(table->AddTablet(tablet));
        }

        if (tablegroup) {
          lock.lock();
          RETURN_NOT_OK(tablegroup->AddChildTable(table->id(), colocation_id));
        }
      }
    }
  }

  // For create transaction table requests with tablespace id, save the tablespace id.
  const auto is_transaction_status_table =
      orig_req->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE;
  if (is_transaction_status_table && req.has_tablespace_id()) {
    table->mutable_metadata()->mutable_dirty()->pb.set_transaction_table_tablespace_id(
        req.tablespace_id());
  }

  // Tables with a transaction should be rolled back if the transaction does not get committed.
  // Store this on the table persistent state until the transaction has been a verified success.
  TransactionMetadata txn;
  bool schedule_ysql_txn_verifier = false;
  if (req.has_transaction() &&
      (FLAGS_enable_transactional_ddl_gc || req.ysql_yb_ddl_rollback_enabled())) {
    table->mutable_metadata()->mutable_dirty()->pb.mutable_transaction()->
        CopyFrom(req.transaction());
    txn = VERIFY_RESULT(TransactionMetadata::FromPB(req.transaction()));
    RSTATUS_DCHECK(!txn.status_tablet.empty(), Corruption, "Given incomplete Transaction");

    // Set the YsqlTxnVerifierState.
    if (req.ysql_yb_ddl_rollback_enabled()) {
      table->mutable_metadata()->mutable_dirty()->pb.add_ysql_ddl_txn_verifier_state()->
        set_contains_create_table_op(true);
      schedule_ysql_txn_verifier = true;
    }
  }

  if (!req.xcluster_source_table_id().empty()) {
    table->mutable_metadata()->mutable_dirty()->pb.set_xcluster_source_table_id(
        req.xcluster_source_table_id());
  }

  if (PREDICT_FALSE(FLAGS_TEST_simulate_slow_table_create_secs > 0) &&
      req.table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    LOG(INFO) << "Simulating slow table creation";
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_simulate_slow_table_create_secs));
  }

  // NOTE: the table and tablets are already locked for write at this point,
  // since the CreateTableInfo/CreateTabletInfo functions leave them in that state.
  // They will get committed at the end of this function.
  // Sanity check: the tables and tablets should all be in "preparing" state.
  CHECK_EQ(SysTablesEntryPB::PREPARING, table->metadata().dirty().pb.state());
  if (FLAGS_TEST_create_table_in_running_state) {
    table->mutable_metadata()->mutable_dirty()->pb.set_state(SysTablesEntryPB::RUNNING);
  }
  TRACE("Inserted new table and tablet info into CatalogManager maps");
  VLOG_WITH_PREFIX(1) << "Inserted new table and tablet info into CatalogManager maps";

  // Clone will create its own tablets as part of repartitioning.
  if (!joining_colocation_group && !req.is_clone()) {
    auto opt_wal_retention = xcluster_manager_->GetDefaultWalRetentionSec(namespace_id);
    if (opt_wal_retention) {
      table->mutable_metadata()->mutable_dirty()->pb.set_wal_retention_secs(*opt_wal_retention);
    }

    for (const auto& tablet : tablets) {
      // If new tablets are created, they will be in PREPARING state.
      CHECK_EQ(SysTabletsEntryPB::PREPARING, tablet->metadata().dirty().pb.state());
    }
  }

  if (FLAGS_enable_pg_cron && IsPgCronJobTable(req)) {
    RETURN_NOT_OK(CreatePgCronService(epoch));
  }

  s = sys_catalog_->Upsert(epoch, table, tablets);
  if (PREDICT_FALSE(!s.ok())) {
    return AbortTableCreation(
        table.get(), tablets, s.CloneAndPrepend("An error occurred while inserting to sys-tablets"),
        resp);
  }
  TRACE("Wrote table and tablets to system table");

  // For index table, insert index info in the indexed table.
  if ((req.has_index_info() || req.has_indexed_table_id())) {
    if (index_backfill_enabled && !req.skip_index_backfill()) {
      if (is_pg_table) {
        // YSQL: start at some permission before backfill.  The real enforcement happens with
        // pg_index system table's indislive and indisready columns.  Choose WRITE_AND_DELETE
        // because it will probably be less confusing.
        index_info.set_index_permissions(INDEX_PERM_WRITE_AND_DELETE);
      } else {
        // YCQL
        index_info.set_index_permissions(INDEX_PERM_DELETE_ONLY);
      }
    }
    if (!indexed_table_write_lock.locked()) {
      TRACE("Locking indexed table");
      indexed_table_write_lock = indexed_table->LockForWrite();
    }
    s = AddIndexInfoToTable(indexed_table, &indexed_table_write_lock, index_info, epoch, resp);
    if (PREDICT_FALSE(!s.ok())) {
      return AbortTableCreation(
          table.get(), tablets, s.CloneAndPrepend("An error occurred while inserting index info"),
          resp);
    }
  }

  // Commit the in-memory state.
  table->mutable_metadata()->CommitMutation();

  for (const auto& tablet : tablets) {
    tablet->mutable_metadata()->CommitMutation();
    // Add the table id to the in-memory vector of table ids on TabletInfo.
    if (joining_colocation_group) {
      tablet->AddTableId(table->id());
    }
  }

  if (joining_colocation_group) {
    auto counter = std::make_shared<std::atomic<size_t>>(tablets.size());
    auto duplicate_counter = FLAGS_TEST_duplicate_addtabletotablet_request
        ? std::make_shared<std::atomic<size_t>>(tablets.size()) : nullptr;
    for (auto& tablet : tablets) {
      auto call = std::make_shared<AsyncAddTableToTablet>(
          master_, AsyncTaskPool(), tablet, table, epoch, counter);
      table->AddTask(call);
      WARN_NOT_OK(ScheduleTask(call), "Failed to send AddTableToTablet request");
      if (FLAGS_TEST_duplicate_addtabletotablet_request) {
        auto duplicate_call = std::make_shared<AsyncAddTableToTablet>(
            master_, AsyncTaskPool(), tablet, table, epoch, duplicate_counter);
        table->AddTask(duplicate_call);
        WARN_NOT_OK(
            ScheduleTask(duplicate_call), "Failed to send duplicate AddTableToTablet request");
      }
    }
  }

  if (req.has_creator_role_name()) {
    const NamespaceName& keyspace_name = req.namespace_().name();
    const TableName& table_name = req.name();
    RETURN_NOT_OK(permissions_manager_->GrantPermissions(
        req.creator_role_name(),
        get_canonical_table(keyspace_name, table_name),
        table_name,
        keyspace_name,
        all_permissions_for_resource(ResourceType::TABLE),
        ResourceType::TABLE,
        resp));
  }

  // Verify Transaction gets committed, which occurs after table create finishes.
  if (req.has_transaction()) {
    if (schedule_ysql_txn_verifier) {
      RETURN_NOT_OK(ScheduleYsqlTxnVerification(table, txn, epoch));
    } else if (PREDICT_TRUE(FLAGS_enable_transactional_ddl_gc)) {
      LOG(INFO) << "Enqueuing table for Transaction Verification: " << req.name();
      ScheduleVerifyTablePgLayer(txn, table, epoch);
    }
  }

  LOG(INFO) << Format("Successfully created $0 $1 in $2 per request from $3",
                      PROTO_PTR_IS_TABLE(orig_req) ? "table" : "index",
                      table->ToString(),
                      ns->ToString(),
                      RequestorString(rpc));
  // Kick the background task to start asynchronously creating tablets and assigning tablet leaders.
  // If the table is joining an existing colocated group there's no work to do.
  if (!joining_colocation_group) {
    background_tasks_->Wake();
  }

  // Add the new table's entry to the in-memory structure cdc_stream_map_.
  // This will be used by the background task to determine if the table needs to associated to an
  // active CDCSDK stream. We do not consider the dummy parent table created for tablegroups.
  if (!colocated || joining_colocation_group) {
    WARN_NOT_OK(
        AddNewTableToCDCDKStreamsMetadata(table->id(), ns->id()),
        "Could not add table: " + table->id() + ", details to required CDCSDK streams");
  }

  if (FLAGS_master_enable_metrics_snapshotter &&
      !(req.table_type() == TableType::YQL_TABLE_TYPE &&
        namespace_id == kSystemNamespaceId &&
        req.name() == kMetricsSnapshotsTableName)) {
    Status s = CreateMetricsSnapshotsTableIfNeeded(epoch, rpc);
    if (!s.ok()) {
      return s.CloneAndPrepend("Error while creating metrics snapshots table");
    }
  }

  // Increment transaction status version if needed.
  if (is_transaction_status_table) {
    RETURN_NOT_OK(IncrementTransactionTablesVersion());
  }

  DVLOG(3) << __PRETTY_FUNCTION__ << " Done.";

  if (FLAGS_TEST_fail_table_creation_at_preparing_state) {
    return STATUS(IllegalState, "Failing table creation at PREPARING state");
  }

  return Status::OK();
}

Status CatalogManager::CreateTableIfNotFound(
    const std::string& namespace_name, const std::string& table_name,
    std::function<Result<CreateTableRequestPB>()> generate_request, const LeaderEpoch& epoch) {
  // If table exists do nothing, otherwise create it.
  if (VERIFY_RESULT(TableExists(namespace_name, table_name))) {
    return Status::OK();
  }

  auto req = VERIFY_RESULT(generate_request());
  DCHECK_EQ(req.namespace_().name(), namespace_name);
  DCHECK_EQ(req.name(), table_name);

  CreateTableResponsePB resp;
  Status s = CreateTable(&req, &resp, /* RpcContext */ nullptr, epoch);
  // We do not lock here so it is technically possible that the table was already created.
  // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
  if (s.ok() && resp.has_error()) {
    s = StatusFromPB(resp.error().status());
  }
  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s;
  }
  return Status::OK();
}

void CatalogManager::ScheduleVerifyTablePgLayer(TransactionMetadata txn,
                                                const TableInfoPtr& table,
                                                const LeaderEpoch& epoch) {
  auto when_done = [this, table, epoch](Result<std::optional<bool>> exists) {
    WARN_NOT_OK(VerifyTablePgLayer(table, exists, epoch), "Failed to verify table");
  };
  TableSchemaVerificationTask::CreateAndStartTask(
      *this, table, txn, std::move(when_done), sys_catalog_, master_->client_future(),
      *master_->messenger(), epoch, false /* ddl_atomicity_enabled */);
}

Status CatalogManager::VerifyTablePgLayer(
    scoped_refptr<TableInfo> table, Result<std::optional<bool>> exists, const LeaderEpoch& epoch) {
  if (!exists.ok()) {
    return exists.status();
  }
  auto opt_exists = exists.get();
  SCHECK(opt_exists.has_value(), IllegalState,
         Substitute("Unexpected opt_exists for $0", table->ToString()));
  // Upon Transaction completion, check pg system table using OID to ensure SUCCESS.
  auto l = table->LockForWrite();
  auto* mutable_table_info = table->mutable_metadata()->mutable_dirty();
  auto& metadata = mutable_table_info->pb;

  SCHECK(
      mutable_table_info->is_running(), Aborted,
      Substitute(
          "Unexpected table state ($0), abandoning transaction GC work for $1",
          SysTablesEntryPB_State_Name(metadata.state()), table->ToString()));

  if (*opt_exists) {
    // Remove the transaction from the entry since we're done processing it.
    metadata.clear_transaction();
    RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table));
    LOG_WITH_PREFIX(INFO) << "Table transaction succeeded: " << table->ToString();

    // Commit the in-memory state.
    l.Commit();
  } else {
    LOG(INFO) << "Table transaction failed, deleting: " << table->ToString();
    // Async enqueue delete.
    DeleteTableRequestPB del_tbl_req;
    del_tbl_req.mutable_table()->set_table_name(table->name());
    del_tbl_req.mutable_table()->set_table_id(table->id());
    del_tbl_req.set_is_index_table(table->is_index());

    RETURN_NOT_OK(background_tasks_thread_pool_->SubmitFunc( [this, del_tbl_req, epoch]() {
      DeleteTableResponsePB del_tbl_resp;
      WARN_NOT_OK(DeleteTable(&del_tbl_req, &del_tbl_resp, nullptr, epoch),
          "Failed to Delete Table with failed transaction");
    }));
  }
  return Status::OK();
}

Result<TabletInfos> CatalogManager::CreateTabletsFromTable(const vector<Partition>& partitions,
                                                           const TableInfoPtr& table,
                                                           SysTabletsEntryPB::State state) {
  TabletInfos tablets;
  for (const Partition& partition : partitions) {
    PartitionPB partition_pb;
    partition.ToPB(&partition_pb);
    tablets.push_back(CreateTabletInfo(table.get(), partition_pb, state));
  }

  // Add the table/tablets to the in-memory map for the assignment.
  RETURN_NOT_OK(table->AddTablets(tablets));
  auto tablet_map_checkout = tablet_map_.CheckOut();
  for (const TabletInfoPtr& tablet : tablets) {
    InsertOrDie(tablet_map_checkout.get_ptr(), tablet->tablet_id(), tablet);
  }

  return tablets;
}

Status CatalogManager::CheckValidPlacementInfo(const PlacementInfoPB& placement_info,
                                               const TSDescriptorVector& ts_descs,
                                               ValidateReplicationInfoResponsePB* resp) {
  size_t num_live_tservers = ts_descs.size();
  size_t num_replicas = GetNumReplicasOrGlobalReplicationFactor(placement_info);
  Status s;
  string msg;

  // Verify that the number of replicas isn't larger than the required number of live tservers.
  // To ensure quorum, we need n/2 + 1 live tservers.
  size_t replica_quorum_needed = num_replicas / 2 + 1;
  if (FLAGS_catalog_manager_check_ts_count_for_create_table &&
      replica_quorum_needed > num_live_tservers) {
    msg = Substitute("Not enough live tablet servers to create table with replication factor $0. "
                     "Need at least $1 tablet servers whereas $2 are alive.",
                     num_replicas, replica_quorum_needed, num_live_tservers);
    LOG(WARNING) << msg
                 << " Placement info: " << placement_info.ShortDebugString()
                 << ", replication factor flag: " << FLAGS_replication_factor;
    s = STATUS(InvalidArgument, msg);
    return SetupError(resp->mutable_error(), MasterErrorPB::REPLICATION_FACTOR_TOO_HIGH, s);
  }

  // Verify that placement requests are reasonable.
  if (!placement_info.placement_blocks().empty()) {
    size_t minimum_sum = 0;
    for (const auto& pb : placement_info.placement_blocks()) {
      minimum_sum += pb.min_num_replicas();
      if (!pb.has_cloud_info()) {
        msg = Substitute("Got placement info without cloud info set: $0", pb.ShortDebugString());
        s = STATUS(InvalidArgument, msg);
        LOG(WARNING) << msg;
        return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
      }
    }
    // Total replicas requested should be at least the sum of minimums
    // requested in individual placement blocks.
    if (minimum_sum > num_replicas) {
      msg = Substitute("Sum of minimum replicas per placement ($0) is greater than num_replicas "
                       " ($1)", minimum_sum, num_replicas);
      s = STATUS(InvalidArgument, msg);
      LOG(WARNING) << msg;
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
    }

    // Verify that there are enough TServers in the requested placements
    // to match the total required replication factor.
    auto allowed_ts = VERIFY_RESULT(FindTServersForPlacementInfo(placement_info, ts_descs));

    // Fail if we don't have enough tablet servers in the areas requested.
    // We need n/2 + 1 for quorum.
    if (allowed_ts.size() < replica_quorum_needed) {
      msg = Substitute("Not enough tablet servers in the requested placements. "
                        "Need at least $0, have $1",
                        replica_quorum_needed, allowed_ts.size());
      s = STATUS(InvalidArgument, msg);
      LOG(WARNING) << msg;
      return SetupError(resp->mutable_error(), MasterErrorPB::REPLICATION_FACTOR_TOO_HIGH, s);
    }

    // Try allocating tservers for the replicas and see if we can place a quorum
    // number of replicas.
    // Essentially, the logic is:
    // 1. We satisfy whatever we can from the minimums.
    // 2. We then satisfy whatever we can from the slack.
    //    Here it doesn't whether where we put the slack replicas as long as
    //    the tservers are chosen from any of the valid placement blocks.
    // Overall, if in this process we are able to place n/2 + 1 replicas
    // then we succeed otherwise we fail.
    size_t total_extra_replicas = num_replicas - minimum_sum;
    size_t total_feasible_replicas = 0;
    size_t total_extra_servers = 0;
    for (const auto& pb : placement_info.placement_blocks()) {
      auto allowed_ts = VERIFY_RESULT(FindTServersForPlacementBlock(pb, ts_descs));
      size_t allowed_ts_size = allowed_ts.size();
      size_t min_num_replicas = pb.min_num_replicas();
      // For every placement block, we can only satisfy upto the number of
      // tservers present in that particular placement block.
      total_feasible_replicas += min(allowed_ts_size, min_num_replicas);
      // Extra tablet servers beyond min_num_replicas will be used to place
      // the extra replicas over and above the minimums.
      if (allowed_ts_size > min_num_replicas) {
        total_extra_servers += allowed_ts_size - min_num_replicas;
      }
    }
    // The total number of extra replicas that we can put cannot be more than
    // the total tablet servers that are extra.
    total_feasible_replicas += min(total_extra_replicas, total_extra_servers);

    // If we place the replicas in accordance with above, we should be able to place
    // at least replica_quorum_needed otherwise we fail.
    if (total_feasible_replicas < replica_quorum_needed) {
      msg = Substitute("Not enough tablet servers in the requested placements. "
                        "Can only find $0 tablet servers for the replicas but need at least "
                        "$1.", total_feasible_replicas, replica_quorum_needed);
      s = STATUS(InvalidArgument, msg);
      LOG(WARNING) << msg;
      return SetupError(resp->mutable_error(), MasterErrorPB::REPLICATION_FACTOR_TOO_HIGH, s);
    }
  }

  return Status::OK();
}

Status CatalogManager::CreateTableInMemory(const CreateTableRequestPB& req,
                                           const Schema& schema,
                                           const PartitionSchema& partition_schema,
                                           const NamespaceId& namespace_id,
                                           const NamespaceName& namespace_name,
                                           const std::vector<Partition>& partitions,
                                           bool colocated,
                                           IsSystemObject system_table,
                                           IndexInfoPB* index_info,
                                           TabletInfos* tablets,
                                           CreateTableResponsePB* resp,
                                           scoped_refptr<TableInfo>* table) {
  // Add the new table in "preparing" state.
  *table = CreateTableInfo(
      req, schema, partition_schema, namespace_id, namespace_name, colocated, index_info);
  const TableId& table_id = (*table)->id();

  VLOG_WITH_PREFIX_AND_FUNC(2)
      << "Table: " << (**table).ToString() << ", create_tablets: " << (tablets ? "YES" : "NO");

  auto table_map_checkout = tables_.CheckOut();
  table_map_checkout->AddOrReplace(*table);
  // Do not add Postgres tables to the name map as the table name is not unique in a namespace.
  if (req.table_type() != PGSQL_TABLE_TYPE) {
    table_names_map_[{namespace_id, req.name()}] = *table;
  }

  if (req.table_type() == TRANSACTION_STATUS_TABLE_TYPE) {
    transaction_table_ids_set_.insert(table_id);
  }

  if (system_table) {
    (*table)->set_is_system();
  }

  if (tablets) {
    if (req.is_clone()) {
      // If this is a cloned table, the tablets will be created by the source tablets when
      // they apply the clone op.
      *tablets = VERIFY_RESULT(
          CreateTabletsFromTable(partitions, *table, SysTabletsEntryPB::CREATING));
      for (auto& tablet : *tablets) {
        tablet->mutable_metadata()->mutable_dirty()->pb.set_created_by_clone(true);
      }
    } else {
      *tablets = VERIFY_RESULT(
          CreateTabletsFromTable(partitions, *table, SysTabletsEntryPB::PREPARING));
    }
  }

  if (resp != nullptr) {
    resp->set_table_id(table_id);
  }

  HandleNewTableId(table_id);

  return Status::OK();
}

Result<bool> CatalogManager::TableExists(
    const std::string& namespace_name, const std::string& table_name) const {
  TableIdentifierPB table_id_pb;
  table_id_pb.set_table_name(table_name);
  table_id_pb.mutable_namespace_()->set_name(namespace_name);
  return DoesTableExist(FindTable(table_id_pb));
}

Status CatalogManager::CreateTransactionStatusTable(
    const CreateTransactionStatusTableRequestPB* req, CreateTransactionStatusTableResponsePB* resp,
    rpc::RpcContext *rpc, const LeaderEpoch& epoch) {
  const string& table_name = req->table_name();
  Status s = CreateTransactionStatusTableInternal(
      rpc, table_name, nullptr /* tablespace_id */, epoch,
      req->has_replication_info() ? &req->replication_info() : nullptr);
  if (s.IsAlreadyPresent()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT, s);
  }
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, s);
  }
  return Status::OK();
}

Status CatalogManager::CreateTransactionStatusTableInternal(
    rpc::RpcContext *rpc, const string& table_name, const TablespaceId* tablespace_id,
    const LeaderEpoch& epoch,
    const ReplicationInfoPB* replication_info) {
  if (VERIFY_RESULT(TableExists(kSystemNamespaceName, table_name))) {
    return STATUS_SUBSTITUTE(AlreadyPresent, "Table already exists: $0", table_name);
  }

  LOG(INFO) << "Creating transaction status table: " << table_name;
  // Set up a CreateTable request internally.
  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(table_name);
  req.mutable_namespace_()->set_name(kSystemNamespaceName);
  req.set_table_type(TableType::TRANSACTION_STATUS_TABLE_TYPE);
  if (tablespace_id) {
    req.set_tablespace_id(*tablespace_id);
  }
  if (replication_info) {
    *req.mutable_replication_info() = *replication_info;
  }

  // Explicitly set the number tablets if the corresponding flag is set, otherwise CreateTable
  // will use the same defaults as for regular tables.
  int num_tablets;
  if (FLAGS_transaction_table_num_tablets > 0) {
    num_tablets = FLAGS_transaction_table_num_tablets;
  } else {
    auto placement_uuid =
        ClusterConfig()->LockForRead()->pb.replication_info().live_replicas().placement_uuid();
    num_tablets = narrow_cast<int>(GetNumLiveTServersForPlacement(placement_uuid) *
                                   FLAGS_transaction_table_num_tablets_per_tserver);
  }
  req.mutable_schema()->mutable_table_properties()->set_num_tablets(num_tablets);

  ColumnSchema hash(kRedisKeyColumnName, DataType::BINARY, ColumnKind::HASH);
  ColumnSchemaToPB(hash, req.mutable_schema()->mutable_columns()->Add());

  Status s = CreateTable(&req, &resp, rpc, epoch);
  // We do not lock here so it is technically possible that the table was already created.
  // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s;
  }

  return Status::OK();
}

Status CatalogManager::AddTransactionStatusTablet(
    const AddTransactionStatusTabletRequestPB* req, AddTransactionStatusTabletResponsePB* resp,
    rpc::RpcContext *rpc, const LeaderEpoch& epoch) {
  TableInfoPtr table;
  TabletInfoPtr old_tablet;
  TabletInfoPtr new_tablet;
  Partition left_partition;
  TableInfo::WriteLock write_lock;
  {
    LockGuard lock(mutex_);
    table = VERIFY_RESULT(FindTableByIdUnlocked(req->table_id()));
    write_lock = table->LockForWrite();

    auto schema = VERIFY_RESULT(table->GetSchema());

    auto split = VERIFY_RESULT(table->FindSplittableHashPartitionForStatusTable());
    old_tablet = std::move(split.tablet);
    left_partition = std::move(split.left);

    Partition old_partition;
    Partition::FromPB(old_tablet->LockForRead()->pb.partition(), &old_partition);

    auto tablets =
        VERIFY_RESULT(CreateTabletsFromTable({split.right}, table));
    SCHECK_EQ(1, tablets.size(), IllegalState, "Mismatch in number of tablets created");

    new_tablet = std::move(tablets[0]);
    SCHECK_EQ(SysTabletsEntryPB::PREPARING, new_tablet->metadata().dirty().pb.state(),
              IllegalState, "New tablet not in PREPARING state");
  }

  table->AddStatusTabletViaSplitPartition(old_tablet, left_partition, new_tablet);
  auto s = sys_catalog_->Upsert(epoch, table);
  if (PREDICT_FALSE(!s.ok())) {
    return s;
  }

  write_lock.Commit();
  TRACE("Wrote table to system table");

  new_tablet->mutable_metadata()->CommitMutation();

  // Increment transaction status version if needed.
  RETURN_NOT_OK(IncrementTransactionTablesVersion());

  DVLOG(3) << __PRETTY_FUNCTION__ << " Done.";
  return Status::OK();
}

bool CatalogManager::DoesTransactionTableExistForTablespace(const TablespaceId& tablespace_id) {
  SharedLock lock(mutex_);
  for (const auto& table_id : transaction_table_ids_set_) {
    auto table = tables_->FindTableOrNull(table_id);
    if (table == nullptr) {
      LOG(DFATAL) << "Table uuid " << table_id
                  << " in transaction_table_ids_set_ but not in table_ids_map_";
      continue;
    }
    auto this_tablespace_id = GetTransactionStatusTableTablespace(table);
    if (this_tablespace_id && *this_tablespace_id == tablespace_id) {
      return true;
    }
  }
  return false;
}

Status CatalogManager::CreateLocalTransactionStatusTableIfNeeded(
    rpc::RpcContext *rpc, const TablespaceId& tablespace_id, const LeaderEpoch& epoch) {
  std::lock_guard lock(tablespace_transaction_table_creation_mutex_);

  if (DoesTransactionTableExistForTablespace(tablespace_id)) {
    VLOG(1) << "Transaction status table already exists, not creating.";
    return Status::OK();
  }

  std::string table_name;
  if (FLAGS_TEST_name_transaction_tables_with_tablespace_id) {
    uint32_t tablespace_oid = VERIFY_RESULT(GetPgsqlTablespaceOid(tablespace_id));
    table_name = kTransactionTablePrefix + std::to_string(tablespace_oid);
  } else {
    std::string uuid;
    RETURN_NOT_OK(yb::Uuid::Generate().ToString(&uuid));
    table_name = kTransactionTablePrefix + uuid;
  }

  return CreateTransactionStatusTableInternal(
      rpc, table_name, &tablespace_id, epoch, nullptr /* replication_info */);
}

Status CatalogManager::CreateGlobalTransactionStatusTableIfNeededForNewTable(
  const CreateTableRequestPB& req, rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  const bool is_pg_catalog_table =
      req.table_type() == PGSQL_TABLE_TYPE && req.is_pg_catalog_table();
  const bool is_transactional = req.schema().table_properties().is_transactional();
  // If this is a transactional table, we need to create the transaction status table (if it does
  // not exist already).
  if (is_transactional && (!is_pg_catalog_table || !FLAGS_create_initial_sys_catalog_snapshot)) {
    RETURN_NOT_OK_PREPEND(CreateGlobalTransactionStatusTableIfNotPresent(rpc, epoch),
        "Error while creating transaction status table");
  } else {
    VLOG(1) << "Not attempting to create a transaction status table:\n"
            << YB_EXPR_TO_STREAM_ONE_PER_LINE(
                kTwoSpaceIndent,
                is_transactional,
                is_pg_catalog_table,
                FLAGS_create_initial_sys_catalog_snapshot);
  }
  return Status::OK();
}

Status CatalogManager::CreateGlobalTransactionStatusTableIfNotPresent(
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  Status s = CreateTransactionStatusTableInternal(
      rpc, kGlobalTransactionsTableName, nullptr /* tablespace_id */, epoch,
      nullptr /* replication_info */);
  if (s.IsAlreadyPresent()) {
    VLOG(1) << "Transaction status table already exists, not creating.";
    return Status::OK();
  }
  return s;
}

Result<TableInfoPtr> CatalogManager::GetGlobalTransactionStatusTable() {
  TableIdentifierPB global_txn_table_identifier;
  global_txn_table_identifier.set_table_name(kGlobalTransactionsTableName);
  global_txn_table_identifier.mutable_namespace_()->set_name(kSystemNamespaceName);
  return FindTable(global_txn_table_identifier);
}

Status CatalogManager::GetGlobalTransactionStatusTablets(
    GetTransactionStatusTabletsResponsePB* resp) {
  auto global_txn_table = VERIFY_RESULT(GetGlobalTransactionStatusTable());

  auto l = global_txn_table->LockForRead();
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(l, resp));

  for (const auto& tablet : VERIFY_RESULT(global_txn_table->GetTablets())) {
    TabletLocationsPB locs_pb;
    RETURN_NOT_OK(BuildLocationsForTablet(tablet, &locs_pb));
    resp->add_global_tablet_id(tablet->tablet_id());
  }

  return Status::OK();
}

Result<std::vector<TableInfoPtr>> CatalogManager::GetPlacementLocalTransactionStatusTables(
    const CloudInfoPB& placement) {
  std::vector<TableInfoPtr> same_placement_transaction_tables;
  auto tablespace_manager = GetTablespaceManager();

  SharedLock lock(mutex_);
  for (const auto& table_id : transaction_table_ids_set_) {
    auto table = tables_->FindTableOrNull(table_id);
    if (table == nullptr) {
      LOG(DFATAL) << "Table uuid " << table_id
                  << " in transaction_table_ids_set_ but not in table_ids_map_";
      continue;
    }
    // system.transaction is filtered out because it cannot have a placement set.
    auto lock = table->LockForRead();
    auto tablespace_id = GetTransactionStatusTableTablespace(table);
    auto cloud_info = lock->pb.replication_info();
    if (!IsReplicationInfoSet(cloud_info)) {
      if (tablespace_id) {
        const auto result = tablespace_manager->GetTablespaceReplicationInfo(*tablespace_id);
        if (!result.ok() || !*result || !IsReplicationInfoSet(**result)) {
          continue;
        }
        cloud_info = **result;
      }
    }
    const auto& txn_table_replicas = cloud_info.live_replicas();
    // Skip transaction tables spanning multiple regions, since using them will incur global
    // latencies. See #11268.
    if (CatalogManagerUtil::DoesPlacementInfoSpanMultipleRegions(txn_table_replicas)) {
      continue;
    }
    if ((FLAGS_TEST_consider_all_local_transaction_tables_local &&
         !txn_table_replicas.placement_blocks().empty()) ||
        CatalogManagerUtil::DoesPlacementInfoContainCloudInfo(txn_table_replicas, placement)) {
      same_placement_transaction_tables.push_back(table);
    }
  }

  return same_placement_transaction_tables;
}

Status CatalogManager::GetPlacementLocalTransactionStatusTablets(
    const std::vector<TableInfoPtr>& placement_local_tables,
    GetTransactionStatusTabletsResponsePB* resp) {
  if (placement_local_tables.empty()) {
    return Status::OK();
  }

  SharedLock lock(mutex_);
  for (const auto& table_info : placement_local_tables) {
    auto lock = table_info->LockForRead();
    for (const auto& tablet : VERIFY_RESULT(table_info->GetTablets())) {
      TabletLocationsPB locs_pb;
      RETURN_NOT_OK(BuildLocationsForTablet(tablet, &locs_pb));
      resp->add_placement_local_tablet_id(tablet->tablet_id());
    }
  }

  return Status::OK();
}

Status CatalogManager::GetTransactionStatusTablets(
    const GetTransactionStatusTabletsRequestPB* req,
    GetTransactionStatusTabletsResponsePB* resp,
    rpc::RpcContext *rpc) {
  for (;;) {
    SCOPED_LEADER_SHARED_LOCK(lock, this);
    auto global_txn_table = VERIFY_RESULT(GetGlobalTransactionStatusTable());
    if (!VERIFY_RESULT(IsCreateTableDone(global_txn_table))) {
      lock.Unlock();
      RETURN_NOT_OK(WaitForCreateTableToFinish(global_txn_table->id(), rpc->GetClientDeadline()));
      continue;
    }

    std::vector<TableInfoPtr> local_tables;
    if (req->has_placement()) {
      local_tables = VERIFY_RESULT(GetPlacementLocalTransactionStatusTables(req->placement()));
      bool need_restart = false;
      for (const auto& table : local_tables) {
        if (!VERIFY_RESULT(IsCreateTableDone(table))) {
          if (!need_restart) {
            need_restart = true;
            lock.Unlock();
          }
          RETURN_NOT_OK(WaitForCreateTableToFinish(table->id(), rpc->GetClientDeadline()));
        }
      }
      if (need_restart) {
        continue;
      }
    }

    RETURN_NOT_OK(GetGlobalTransactionStatusTablets(resp));
    RETURN_NOT_OK(GetPlacementLocalTransactionStatusTablets(local_tables, resp));

    return Status::OK();
  }
}

Status CatalogManager::CreateMetricsSnapshotsTableIfNeeded(
    const LeaderEpoch& epoch, rpc::RpcContext* rpc) {
  if (VERIFY_RESULT(TableExists(kSystemNamespaceName, kMetricsSnapshotsTableName))) {
    return Status::OK();
  }

  // Set up a CreateTable request internally.
  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(kMetricsSnapshotsTableName);
  req.mutable_namespace_()->set_name(kSystemNamespaceName);
  req.set_table_type(TableType::YQL_TABLE_TYPE);

  // Explicitly set the number tablets if the corresponding flag is set, otherwise CreateTable
  // will use the same defaults as for regular tables.
  if (FLAGS_metrics_snapshots_table_num_tablets > 0) {
    req.mutable_schema()->mutable_table_properties()->set_num_tablets(
        FLAGS_metrics_snapshots_table_num_tablets);
  }

  // Schema description: "node" refers to tserver uuid. "entity_type" can be either
  // "tserver" or "table". "entity_id" is uuid of corresponding tserver or table.
  // "metric" is the name of the metric and "value" is its val. "ts" is time at
  // which the snapshot was recorded. "details" is a json column for future extensibility.

  YBSchemaBuilder schema_builder;
  schema_builder.AddColumn("node")->Type(DataType::STRING)->HashPrimaryKey();
  schema_builder.AddColumn("entity_type")->Type(DataType::STRING)->PrimaryKey();
  schema_builder.AddColumn("entity_id")->Type(DataType::STRING)->PrimaryKey();
  schema_builder.AddColumn("metric")->Type(DataType::STRING)->PrimaryKey();
  schema_builder.AddColumn("ts")->Type(DataType::TIMESTAMP)->PrimaryKey(SortingType::kDescending);
  schema_builder.AddColumn("value")->Type(DataType::INT64);
  schema_builder.AddColumn("details")->Type(DataType::JSONB);

  YBSchema ybschema;
  RETURN_NOT_OK(schema_builder.Build(&ybschema));

  auto schema = yb::client::internal::GetSchema(ybschema);
  SchemaToPB(schema, req.mutable_schema());

  Status s = CreateTable(&req, &resp, rpc, epoch);
  // We do not lock here so it is technically possible that the table was already created.
  // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
  if (s.IsAlreadyPresent()) {
    return Status::OK();
  }
  return s;
}

Status CatalogManager::CreateStatefulService(
    const StatefulServiceKind& service_kind, const client::YBSchema& yb_schema,
    const LeaderEpoch& epoch) {
  const string table_name = GetStatefulServiceTableName(service_kind);
  // If the service table exists do nothing, otherwise create it.
  if (VERIFY_RESULT(TableExists(kSystemNamespaceName, table_name))) {
    return STATUS_FORMAT(AlreadyPresent, "Table $0 already created", table_name);
  }

  LOG(INFO) << "Creating stateful service table " << table_name << " for service "
            << StatefulServiceKind_Name(service_kind);

  // Set up a CreateTable request internally.
  CreateTableRequestPB req;
  CreateTableResponsePB resp;
  req.set_name(table_name);
  req.mutable_namespace_()->set_name(kSystemNamespaceName);
  req.set_table_type(TableType::YQL_TABLE_TYPE);
  req.set_num_tablets(1);
  req.add_hosted_stateful_services(service_kind);

  auto schema = yb::client::internal::GetSchema(yb_schema);

  SchemaToPB(schema, req.mutable_schema());

  req.mutable_schema()->mutable_table_properties()->set_num_tablets(1);

  Status s = CreateTable(&req, &resp, /* RpcContext */ nullptr, epoch);
  return s;
}

Status CatalogManager::CreateTestEchoService(const LeaderEpoch& epoch) {
  static bool service_created = false;
  if (service_created) {
    return Status::OK();
  }

  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn(kTestEchoTimestamp)->HashPrimaryKey()->Type(DataType::TIMESTAMP);
  schema_builder.AddColumn(kTestEchoNodeId)->Type(DataType::STRING);
  schema_builder.AddColumn(kTestEchoMessage)->Type(DataType::STRING);

  client::YBSchema yb_schema;
  CHECK_OK(schema_builder.Build(&yb_schema));

  auto s = CreateStatefulService(StatefulServiceKind::TEST_ECHO, yb_schema, epoch);
  // It is possible that the table was already created. If so, there is nothing to do so we just
  // ignore the "AlreadyPresent" error.
  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s;
  }

  service_created = true;
  return Status::OK();
}

Status CatalogManager::CreatePgAutoAnalyzeService(const LeaderEpoch& epoch) {
  static bool pg_auto_analyze_service_created = false;
  if (pg_auto_analyze_service_created) {
    return Status::OK();
  }

  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn(kPgAutoAnalyzeTableId)->HashPrimaryKey()->Type(DataType::STRING);
  schema_builder.AddColumn(kPgAutoAnalyzeMutations)->Type(DataType::INT64);
  schema_builder.AddColumn(kPgAutoAnalyzeLastAnalyzeInfo)->Type(DataType::JSONB);
  schema_builder.AddColumn(kPgAutoAnalyzeCurrentAnalyzeInfo)->Type(DataType::JSONB);

  client::YBSchema yb_schema;
  CHECK_OK(schema_builder.Build(&yb_schema));

  auto s = CreateStatefulService(StatefulServiceKind::PG_AUTO_ANALYZE, yb_schema, epoch);
  // It is possible that the table was already created. If so, there is nothing to do so we just
  // ignore the "AlreadyPresent" error.
  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s;
  }

  pg_auto_analyze_service_created = true;
  return Status::OK();
}

Status CatalogManager::CreatePgCronService(const LeaderEpoch& epoch) {
  if (pg_cron_service_created_) {
    return Status::OK();
  }

  // Use a generic schema that can be extended later on.
  client::YBSchemaBuilder schema_builder;
  schema_builder.AddColumn("id")->HashPrimaryKey()->Type(DataType::INT64);
  schema_builder.AddColumn("data")->Type(DataType::JSONB);

  client::YBSchema yb_schema;
  CHECK_OK(schema_builder.Build(&yb_schema));

  auto s = CreateStatefulService(StatefulServiceKind::PG_CRON_LEADER, yb_schema, epoch);
  // It is possible that the table was already created. If so, there is nothing to do so we just
  // ignore the "AlreadyPresent" error.
  if (!s.ok() && !s.IsAlreadyPresent()) {
    return s;
  }

  pg_cron_service_created_ = true;
  return Status::OK();
}

namespace {

TableIdentifierPB GetTransactionStatusTableId() {
  TableIdentifierPB table_id;

  table_id.set_table_name(kGlobalTransactionsTableName);
  table_id.mutable_namespace_()->set_name(kSystemNamespaceName);
  return table_id;
}

TableIdentifierPB GetMetricsSnapshotsTableId() {
  TableIdentifierPB table_id;

  table_id.set_table_name(kMetricsSnapshotsTableName);
  table_id.mutable_namespace_()->set_name(kSystemNamespaceName);
  table_id.mutable_namespace_()->set_database_type(YQLDatabase::YQL_DATABASE_CQL);
  return table_id;
}

}  // namespace

Result<IsOperationDoneResult> CatalogManager::IsCreateTableDone(const TableInfoPtr& table) {
  TRACE("Locking table");
  auto l = table->LockForRead();
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(l));
  const auto& pb = l->pb;

  TRACE("Verify if the table creation is in progress for $0", table->ToString());
  VLOG_WITH_FUNC(1) << table->ToString();
  if (table->IsCreateInProgress()) {
    // Set any current errors, if we are experiencing issues creating the table. This will be
    // bubbled up to the MasterService layer. If it is an error, it gets wrapped around in
    // MasterErrorPB::UNKNOWN_ERROR.
    // For master only tests running with TEST_create_table_in_running_state and we expect errors
    // since tablets cannot be assigned, so ignore those.
    if (!FLAGS_TEST_create_table_in_running_state) {
      auto status = table->GetCreateTableErrorStatus();
      if (!status.ok()) {
        return IsOperationDoneResult::Done(std::move(status));
      }
    }
    return IsOperationDoneResult::NotDone();
  }

  // If this is an index, we are not done until the index is in the indexed table's schema.  An
  // exception is YSQL system table indexes, which don't get added to their indexed tables' schemas.
  if (IsIndex(pb)) {
    auto& indexed_table_id = GetIndexedTableId(pb);
    // For user indexes (which add index info to indexed table's schema),
    // - if this index is created without backfill,
    //   - waiting for the index to be in the indexed table's schema is sufficient, and, by that
    //     point, things are fully created.
    // - if this index is created with backfill
    //   - and it's YCQL,
    //     - waiting for the index to be in the indexed table's schema means waiting for the
    //       DELETE_ONLY index permission, and it's fine to return to the client before the index
    //       gets the rest of the permissions because the expectation is that backfill will be
    //       completed asynchronously.
    //   - and it's YSQL,
    //     - waiting for the index to be in the indexed table's schema means just that (DocDB index
    //       permissions don't really matter for YSQL besides being used for backfill purposes), and
    //       it's a signal for postgres to continue the index backfill process, activating index
    //       state flags then later triggering backfill and so on.
    // For YSQL system indexes (which don't add index info to indexed table's schema),
    // - there's nothing additional to wait on.
    // Therefore, the only thing needed here is to check whether the index info is in the indexed
    // table's schema for user indexes.
    if (pb.table_type() == YQL_TABLE_TYPE ||
        (pb.table_type() == PGSQL_TABLE_TYPE && table->IsUserCreated(l))) {
      GetTableSchemaRequestPB get_schema_req;
      GetTableSchemaResponsePB get_schema_resp;
      get_schema_req.mutable_table()->set_table_id(indexed_table_id);
      RETURN_NOT_OK(GetTableSchemaInternal(
          &get_schema_req, &get_schema_resp, /* always_get_fully_applied_indexes= */ true));

      bool done = false;
      for (const auto& index : get_schema_resp.indexes()) {
        if (index.has_table_id() && index.table_id() == table->id()) {
          done = true;
          break;
        }
      }

      if (!done) {
        VLOG(1) << "Indexed table is not yet updated";
        return IsOperationDoneResult::NotDone();
      }
    }
  }

  // Sanity check that this table is present in system.partitions if it is a YCQL table.
  // Only check if we are automatically generating the vtable on changes. If we are creating via
  // the bg task, then see below where we invalidate the cache.
  if (DCHECK_IS_ON() && IsYcqlTable(*table) &&
      YQLPartitionsVTable::ShouldGeneratePartitionsVTableOnChanges() &&
      FLAGS_TEST_catalog_manager_check_yql_partitions_exist_for_is_create_table_done) {
    auto schema = VERIFY_RESULT(table->GetSchema());
    // Ignore master only tests as they cannot fully create tablets.
    if (!FLAGS_TEST_create_table_in_running_state) {
      DCHECK(GetYqlPartitionsVtable().CheckTableIsPresent(table->id(), table->NumPartitions()));
    }
  }

  // On create table, invalidate the yql system.partitions cache - this will cause the next
  // partitions query or the next bg thread rebuild to regenerate the cache and include the new
  // table + tablets.
  if (FLAGS_invalidate_yql_partitions_cache_on_create_table && IsYcqlTable(*table) &&
      YQLPartitionsVTable::ShouldGeneratePartitionsVTableWithBgTask()) {
    GetYqlPartitionsVtable().InvalidateCache();
  }

  // If this is a transactional table we are not done until the transaction status table is created.
  // However, if we are currently initializing the system catalog snapshot, we don't create the
  // transactions table.
  if (!FLAGS_create_initial_sys_catalog_snapshot &&
      pb.schema().table_properties().is_transactional()) {
    auto txn_status_table = VERIFY_RESULT(FindTable(GetTransactionStatusTableId()));
    auto is_create_done = VERIFY_RESULT(IsCreateTableDone(txn_status_table));
    if (!is_create_done) {
      VLOG(1) << "Transaction status table is not yet ready: " << is_create_done.ToString();
      return is_create_done;
    }
  }

  // We are not done until the metrics snapshots table is created.
  if (FLAGS_master_enable_metrics_snapshotter &&
      !(table->GetTableType() == TableType::YQL_TABLE_TYPE &&
        table->namespace_id() == kSystemNamespaceId &&
        table->name() == kMetricsSnapshotsTableName)) {
    auto metric_snapshot_table = VERIFY_RESULT(FindTable(GetMetricsSnapshotsTableId()));
    auto is_create_done = VERIFY_RESULT(IsCreateTableDone(metric_snapshot_table));
    if (!is_create_done) {
      VLOG(1) << "Metrics snapshot table is not yet ready: " << is_create_done.ToString();
      return is_create_done;
    }
  }

  return IsOperationDoneResult::Done();
}

Status CatalogManager::IsCreateTableDone(const IsCreateTableDoneRequestPB* req,
                                         IsCreateTableDoneResponsePB* resp) {
  TRACE("Looking up table");
  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTable(req->table()));
  auto is_done = VERIFY_RESULT(IsCreateTableDone(table));
  resp->set_done(is_done.done());
  return std::move(is_done.status());
}

Status CatalogManager::IsCreateTableInProgress(const TableId& table_id,
                                               CoarseTimePoint deadline,
                                               bool* create_in_progress) {
  DCHECK_ONLY_NOTNULL(create_in_progress);
  DCHECK(!table_id.empty());
  TableIdentifierPB table_ident;
  table_ident.set_table_id(table_id);

  TRACE("Looking up table");
  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTable(table_ident));
  auto is_done = VERIFY_RESULT(IsCreateTableDone(table));
  *create_in_progress = !is_done.done();

  return is_done.status();
}

Status CatalogManager::WaitForCreateTableToFinish(
    const TableId& table_id, CoarseTimePoint deadline) {
  return RetryFunc(
      deadline, "Waiting on Create Table to be completed", "Timed out waiting for Table Creation",
      std::bind(&CatalogManager::IsCreateTableInProgress, this, table_id, _1, _2));
}

Status CatalogManager::IsAlterTableInProgress(const TableId& table_id,
                                               CoarseTimePoint deadline,
                                               bool* alter_in_progress) {
  DCHECK_ONLY_NOTNULL(alter_in_progress);
  DCHECK(!table_id.empty());

  IsAlterTableDoneRequestPB req;
  IsAlterTableDoneResponsePB resp;

  req.mutable_table()->set_table_id(table_id);
  RETURN_NOT_OK(IsAlterTableDone(&req, &resp));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  *alter_in_progress = !resp.done();
  return Status::OK();
}

Status CatalogManager::WaitForAlterTableToFinish(
    const TableId& table_id, CoarseTimePoint deadline) {
  return WaitFor(
      [&table_id, &deadline, this]() -> Result<bool> {
        bool alter_in_progress = false;

        auto s = IsAlterTableInProgress(table_id, deadline, &alter_in_progress);
        if (!s.ok()) {
          return false;
        }

        return !alter_in_progress;
      },
      deadline - CoarseMonoClock::now(),
      Format("Waiting on Alter Table to be completed for table_id: $0", table_id),
      100ms /* initial_delay */, 1 /* delay_multiplier */);
}

std::string CatalogManager::GenerateId(boost::optional<const SysRowEntryType> entity_type) {
  SharedLock lock(mutex_);
  return GenerateIdUnlocked(entity_type);
}

std::string CatalogManager::GenerateIdUnlocked(
    boost::optional<const SysRowEntryType> entity_type) {
  while (true) {
    // Generate id and make sure it is unique within its category.
    std::string id = GenerateObjectId();
    if (!entity_type) {
      return id;
    }
    switch (*entity_type) {
      case SysRowEntryType::NAMESPACE:
        if (FindPtrOrNull(namespace_ids_map_, id) == nullptr) return id;
        break;
      case SysRowEntryType::TABLE:
        if (tables_->FindTableOrNull(id) == nullptr) return id;
        break;
      case SysRowEntryType::TABLET:
        if (FindPtrOrNull(*tablet_map_, id) == nullptr) return id;
        break;
      case SysRowEntryType::UDTYPE:
        if (FindPtrOrNull(udtype_ids_map_, id) == nullptr) return id;
        break;
      case SysRowEntryType::CLONE_STATE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT:
        return id;
      case SysRowEntryType::CDC_STREAM: FALLTHROUGH_INTENDED;
      case SysRowEntryType::CLUSTER_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::ROLE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::REDIS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNIVERSE_REPLICATION: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SYS_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT_SCHEDULE: FALLTHROUGH_INTENDED;
      case SysRowEntryType::DDL_LOG_ENTRY: FALLTHROUGH_INTENDED;
      case SysRowEntryType::SNAPSHOT_RESTORATION: FALLTHROUGH_INTENDED;
      case SysRowEntryType::XCLUSTER_SAFE_TIME: FALLTHROUGH_INTENDED;
      case SysRowEntryType::XCLUSTER_CONFIG: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNIVERSE_REPLICATION_BOOTSTRAP: FALLTHROUGH_INTENDED;
      case SysRowEntryType::XCLUSTER_OUTBOUND_REPLICATION_GROUP: FALLTHROUGH_INTENDED;
      case SysRowEntryType::TSERVER_REGISTRATION: FALLTHROUGH_INTENDED;
      case SysRowEntryType::OBJECT_LOCK_ENTRY: FALLTHROUGH_INTENDED;
      case SysRowEntryType::UNKNOWN:
        LOG(DFATAL) << "Invalid id type: " << *entity_type;
        return id;
    }
  }
}

scoped_refptr<TableInfo> CatalogManager::CreateTableInfo(const CreateTableRequestPB& req,
                                                         const Schema& schema,
                                                         const PartitionSchema& partition_schema,
                                                         const NamespaceId& namespace_id,
                                                         const NamespaceName& namespace_name,
                                                         bool colocated,
                                                         IndexInfoPB* index_info) {
  DCHECK(schema.has_column_ids());
  TableId table_id
      = !req.table_id().empty() ? req.table_id() : GenerateIdUnlocked(SysRowEntryType::TABLE);
  scoped_refptr<TableInfo> table = NewTableInfo(table_id, colocated);
  if (req.has_tablespace_id()) {
    table->SetTablespaceIdForTableCreation(req.tablespace_id());
  }
  table->mutable_metadata()->StartMutation();
  SysTablesEntryPB *metadata = &table->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_state(SysTablesEntryPB::PREPARING);
  metadata->set_name(req.name());
  metadata->set_table_type(req.table_type());
  metadata->set_namespace_id(namespace_id);
  if (!FLAGS_TEST_create_table_with_empty_namespace_name) {
    metadata->set_namespace_name(namespace_name);
  }
  metadata->set_version(0);
  metadata->set_next_column_id(ColumnId(schema.max_col_id() + 1));
  *metadata->mutable_hosted_stateful_services() = req.hosted_stateful_services();

  if (req.has_replication_info()) {
    metadata->mutable_replication_info()->CopyFrom(req.replication_info());
  }
  // Use the Schema object passed in, since it has the column IDs already assigned,
  // whereas the user request PB does not.
  SchemaToPB(schema, metadata->mutable_schema());
  if (FLAGS_TEST_create_table_with_empty_pgschema_name) {
    // Use empty string (default proto val) so that this passes has_pgschema_name() checks.
    metadata->mutable_schema()->set_pgschema_name("");
  }
  partition_schema.ToPB(metadata->mutable_partition_schema());
  // For index table, set index details (indexed table id and whether the index is local).
  if (req.has_index_info()) {
    metadata->mutable_index_info()->CopyFrom(req.index_info());

    // Set the deprecated fields also for compatibility reasons.
    metadata->set_indexed_table_id(req.index_info().indexed_table_id());
    metadata->set_is_local_index(req.index_info().is_local());
    metadata->set_is_unique_index(req.index_info().is_unique());

    // Setup index info.
    if (index_info != nullptr) {
      index_info->set_table_id(table->id());
      metadata->mutable_index_info()->CopyFrom(*index_info);
    }
  } else if (req.has_indexed_table_id()) {
    // Read data from the deprecated field and update the new fields.
    metadata->mutable_index_info()->set_indexed_table_id(req.indexed_table_id());
    metadata->mutable_index_info()->set_is_local(req.is_local_index());
    metadata->mutable_index_info()->set_is_unique(req.is_unique_index());

    // Set the deprecated fields also for compatibility reasons.
    metadata->set_indexed_table_id(req.indexed_table_id());
    metadata->set_is_local_index(req.is_local_index());
    metadata->set_is_unique_index(req.is_unique_index());

    // Setup index info.
    if (index_info != nullptr) {
      index_info->set_table_id(table->id());
      metadata->mutable_index_info()->CopyFrom(*index_info);
    }
  }

  if (req.is_pg_shared_table()) {
    metadata->set_is_pg_shared_table(true);
  }

  if (req.is_matview()) {
    metadata->set_is_matview(true);
  }

  if (req.has_pg_table_id()) {
    metadata->set_pg_table_id(req.pg_table_id());
  }

  if (colocated) {
    metadata->set_colocated(true);
  }

  return table;
}

TabletInfoPtr CatalogManager::CreateTabletInfo(TableInfo* table,
                                               const PartitionPB& partition,
                                               SysTabletsEntryPB::State state) {
  auto tablet = std::make_shared<TabletInfo>(table, GenerateIdUnlocked(SysRowEntryType::TABLET));
  VLOG_WITH_PREFIX_AND_FUNC(2)
      << "Table: " << table->ToString() << ", tablet: " << tablet->ToString();

  tablet->mutable_metadata()->StartMutation();
  SysTabletsEntryPB *metadata = &tablet->mutable_metadata()->mutable_dirty()->pb;
  metadata->set_state(state);
  metadata->mutable_partition()->CopyFrom(partition);
  metadata->set_table_id(table->id());
  if (FLAGS_use_parent_table_id_field && !table->is_system()) {
    tablet->SetTableIds({table->id()});
    metadata->set_hosted_tables_mapped_by_parent_id(true);
  } else {
    // This is important: we are setting the first table id in the table_ids list
    // to be the id of the original table that creates the tablet.
    metadata->add_table_ids(table->id());
  }

  ConsensusStatePB* cstate = metadata->mutable_committed_consensus_state();
  cstate->set_current_term(kMinimumTerm);
  consensus::RaftConfigPB* config = cstate->mutable_config();
  config->set_opid_index(consensus::kInvalidOpIdIndex);

  return tablet;
}

Status CatalogManager::RemoveTableIdsFromTabletInfo(
    TabletInfoPtr tablet_info, const std::unordered_set<TableId>& tables_to_remove,
    const LeaderEpoch& epoch) {
  auto tablet_lock = tablet_info->LockForWrite();
  if (tablet_lock->pb.hosted_tables_mapped_by_parent_id()) {
    tablet_info->RemoveTableIds(tables_to_remove);
  } else {
    google::protobuf::RepeatedPtrField<std::string> new_table_ids;
    for (const auto& table_id : tablet_lock->pb.table_ids()) {
      if (tables_to_remove.find(table_id) == tables_to_remove.end()) {
        *new_table_ids.Add() = std::move(table_id);
      }
    }
    tablet_lock.mutable_data()->pb.mutable_table_ids()->Swap(&new_table_ids);
    RETURN_NOT_OK(sys_catalog_->Upsert(epoch, tablet_info));
    tablet_lock.Commit();
  }
  return Status::OK();
}

Result<scoped_refptr<TableInfo>> CatalogManager::FindTable(
    const TableIdentifierPB& table_identifier, bool include_deleted) const {
  SharedLock lock(mutex_);
  return FindTableUnlocked(table_identifier, include_deleted);
}

Result<scoped_refptr<TableInfo>> CatalogManager::FindTableUnlocked(
    const TableIdentifierPB& table_identifier, bool include_deleted) const {
  if (table_identifier.has_table_id()) {
    return FindTableByIdUnlocked(table_identifier.table_id(), include_deleted);
  }

  if (table_identifier.has_table_name()) {
    auto namespace_info = VERIFY_RESULT(FindNamespaceUnlocked(table_identifier.namespace_()));

    // We can't lookup YSQL table by name because Postgres concept of "schemas"
    // introduces ambiguity.
    if (namespace_info->database_type() == YQL_DATABASE_PGSQL) {
      return STATUS_FORMAT(
          InvalidArgument, "Cannot lookup YSQL table $0 by name", table_identifier);
    }

    auto it = table_names_map_.find({namespace_info->id(), table_identifier.table_name()});
    if (it == table_names_map_.end() || (!include_deleted && it->second->is_deleted())) {
      return STATUS_EC_FORMAT(
          NotFound, MasterError(MasterErrorPB::OBJECT_NOT_FOUND),
          "Table $0.$1 not found", namespace_info->name(), table_identifier.table_name());
    }
    return it->second;
  }

  return STATUS(InvalidArgument, "Neither table id or table name are specified",
                table_identifier.ShortDebugString());
}

Result<scoped_refptr<TableInfo>> CatalogManager::FindTableById(
    const TableId& table_id) const {
  SharedLock lock(mutex_);
  return FindTableByIdUnlocked(table_id);
}

Result<scoped_refptr<TableInfo>> CatalogManager::FindTableByIdUnlocked(
    const TableId& table_id, bool include_deleted) const {
  auto table = tables_->FindTableOrNull(table_id);
  if (table == nullptr || (!include_deleted && table->is_deleted())) {
    return STATUS_EC_FORMAT(
        NotFound, MasterError(MasterErrorPB::OBJECT_NOT_FOUND),
        "Table with identifier $0 not found", table_id);
  }
  return table;
}

Result<TableId> CatalogManager::GetColocatedTableId(
    const TablegroupId& tablegroup_id, ColocationId colocation_id) const {
  SharedLock lock(mutex_);
  const auto* tablegroup = tablegroup_manager_->Find(tablegroup_id);
  SCHECK(tablegroup, NotFound, Substitute("Tablegroup with ID $0 not found", tablegroup_id));
  return tablegroup->GetChildTableId(colocation_id);
}

Result<scoped_refptr<NamespaceInfo>> CatalogManager::FindNamespaceById(
    const NamespaceId& id) const {
  SharedLock lock(mutex_);
  return FindNamespaceByIdUnlocked(id);
}

Result<scoped_refptr<NamespaceInfo>> CatalogManager::FindNamespaceByIdUnlocked(
    const NamespaceId& id) const {
  auto it = namespace_ids_map_.find(id);
  if (it == namespace_ids_map_.end()) {
    VLOG_WITH_FUNC(4) << "Not found: " << id << "\n" << GetStackTrace();
    return STATUS(NotFound, "Keyspace identifier not found", id,
                  MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
  }
  return it->second;
}

Result<scoped_refptr<NamespaceInfo>> CatalogManager::FindNamespaceUnlocked(
    const NamespaceIdentifierPB& ns_identifier) const {
  if (ns_identifier.has_id()) {
    return FindNamespaceByIdUnlocked(ns_identifier.id());
  }

  if (ns_identifier.has_name()) {
    auto db = GetDatabaseType(ns_identifier);
    auto it = namespace_names_mapper_[db].find(ns_identifier.name());
    if (it == namespace_names_mapper_[db].end()) {
      return STATUS(
          NotFound,
          Format("$0 keyspace name not found", ShortDatabaseType(db)),
          ns_identifier.name(),
          MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
    }
    return it->second;
  }

  LOG(DFATAL) << __func__ << ": " << ns_identifier.ShortDebugString() << ", \n" << GetStackTrace();
  return STATUS(NotFound, "Neither keyspace id nor keyspace name is specified",
                ns_identifier.ShortDebugString(), MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
}

Result<NamespaceInfoPtr> CatalogManager::FindNamespace(
    const NamespaceIdentifierPB& ns_identifier) const {
  SharedLock lock(mutex_);
  return FindNamespaceUnlocked(ns_identifier);
}

Result<scoped_refptr<UDTypeInfo>> CatalogManager::FindUDTypeById(
    const UDTypeId& udt_id) const {
  SharedLock lock(mutex_);
  return FindUDTypeByIdUnlocked(udt_id);
}

Result<scoped_refptr<UDTypeInfo>> CatalogManager::FindUDTypeByIdUnlocked(
    const UDTypeId& udt_id) const {
  scoped_refptr<UDTypeInfo> tp = FindPtrOrNull(udtype_ids_map_, udt_id);
  if (tp == nullptr) {
    VLOG_WITH_FUNC(4) << "UDType not found: " << udt_id << "\n" << GetStackTrace();
    return STATUS(NotFound, "UDType identifier not found", udt_id,
                  MasterError(MasterErrorPB::TYPE_NOT_FOUND));
  }
  return tp;
}

Result<TableDescription> CatalogManager::DescribeTable(
    const TableIdentifierPB& table_identifier, bool succeed_if_create_in_progress) {
  TRACE("Looking up table");
  return DescribeTable(VERIFY_RESULT(FindTable(table_identifier)), succeed_if_create_in_progress);
}

Result<TableDescription> CatalogManager::DescribeTable(
    const TableInfoPtr& table_info, bool succeed_if_create_in_progress) {
  TableDescription result;
  result.table_info = table_info;
  NamespaceId namespace_id;
  {
    TRACE("Locking table");
    auto l = table_info->LockForRead();

    if (!succeed_if_create_in_progress && table_info->IsCreateInProgress()) {
      return STATUS(IllegalState, "Table creation is in progress", table_info->ToString(),
                    MasterError(MasterErrorPB::TABLE_CREATION_IS_IN_PROGRESS));
    }

    result.tablet_infos = VERIFY_RESULT(table_info->GetTablets());

    namespace_id = table_info->namespace_id();
  }

  TRACE("Looking up namespace");
  result.namespace_info = VERIFY_RESULT(FindNamespaceById(namespace_id));

  return result;
}

Result<string> CatalogManager::GetPgSchemaName(
    const TableId& table_id, const PersistentTableInfo& table_info) {
  RSTATUS_DCHECK_EQ(table_info.GetTableType(), PGSQL_TABLE_TYPE, InternalError,
                    Format("Expected YSQL table, got: $0", table_info.GetTableType()));

  uint32_t database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(table_info.namespace_id()));
  uint32_t relfilenode_oid = VERIFY_RESULT(GetPgsqlTableOid(table_id));
  uint32_t pg_table_oid = VERIFY_RESULT(table_info.GetPgTableOid(table_id));

  // If this is a rewritten table, confirm that the relfilenode oid of pg_class entry with
  // OID pg_table_oid is table_oid.
  if (pg_table_oid != relfilenode_oid) {
    uint32_t pg_class_relfilenode_oid = VERIFY_RESULT(
        sys_catalog_->ReadPgClassColumnWithOidValue(
            database_oid,
            pg_table_oid,
            "relfilenode"));
    if (pg_class_relfilenode_oid != relfilenode_oid) {
      // This must be an orphaned table from a failed rewrite.
      return STATUS(NotFound, kRelnamespaceNotFoundErrorStr +
          std::to_string(pg_table_oid));
    }
  }

  const uint32_t relnamespace_oid = VERIFY_RESULT(
      sys_catalog_->ReadPgClassColumnWithOidValue(database_oid, pg_table_oid, "relnamespace"));

  if (relnamespace_oid == kPgInvalidOid) {
    return STATUS(NotFound, kRelnamespaceNotFoundErrorStr +
        std::to_string(pg_table_oid));
  }

  return sys_catalog_->ReadPgNamespaceNspname(database_oid, relnamespace_oid);
}

Result<std::unordered_map<string, uint32_t>> CatalogManager::GetPgAttNameTypidMap(
    const TableId& table_id, const PersistentTableInfo& table_info) {
  RSTATUS_DCHECK_EQ(
      table_info.pb.table_type(), PGSQL_TABLE_TYPE, InternalError,
      Format("Expected YSQL table, got: $0", table_info.pb.table_type()));
  const uint32_t database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(table_info.namespace_id()));
  uint32_t pg_table_oid = VERIFY_RESULT(table_info.GetPgTableOid(table_id));
  return sys_catalog_->ReadPgAttNameTypidMap(database_oid, pg_table_oid);
}

Result<std::unordered_map<uint32_t, PgTypeInfo>> CatalogManager::GetPgTypeInfo(
    const scoped_refptr<NamespaceInfo>& namespace_info, vector<uint32_t>* type_oids) {
  namespace_info->LockForRead();
  RSTATUS_DCHECK_EQ(
      namespace_info->database_type(), YQL_DATABASE_PGSQL, InternalError,
      Format("Expected YSQL database, got: $0", namespace_info->database_type()));
  const uint32_t database_oid = VERIFY_RESULT(GetPgsqlDatabaseOid(namespace_info->id()));
  return sys_catalog_->ReadPgTypeInfo(database_oid, type_oids);
}

// Truncate a Table.
Status CatalogManager::TruncateTable(const TruncateTableRequestPB* req,
                                     TruncateTableResponsePB* resp,
                                     rpc::RpcContext* rpc,
                                     const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing TruncateTable request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  for (int i = 0; i < req->table_ids_size(); i++) {
    RETURN_NOT_OK(TruncateTable(req->table_ids(i), resp, rpc, epoch));
  }

  return Status::OK();
}

Status CatalogManager::TruncateTable(const TableId& table_id,
                                     TruncateTableResponsePB* resp,
                                     rpc::RpcContext* rpc,
                                     const LeaderEpoch& epoch) {
  // Lookup the table and verify if it exists.
  TRACE(Substitute("Looking up object by id $0", table_id));
  scoped_refptr<TableInfo> table;
  {
    SharedLock lock(mutex_);
    table = tables_->FindTableOrNull(table_id);
    if (table == nullptr) {
      Status s = STATUS_SUBSTITUTE(NotFound, "The object with id $0 does not exist", table_id);
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
    }
  }

  TRACE(Substitute("Locking object with id $0", table_id));
  auto l = table->LockForRead();
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(l, resp));

  // Truncate on a colocated table should not hit master because it should be handled by a write
  // DML that creates a table-level tombstone.
  RSTATUS_DCHECK(!table->IsColocatedUserTable(),
                 InternalError,
                 Format("Cannot truncate colocated table $0 on master", table->name()));

  if (FLAGS_disable_truncate_table) {
    return STATUS(
        NotSupported,
        "TRUNCATE table is disallowed",
        table_id,
        MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  if (!FLAGS_enable_truncate_on_pitr_table) {
      // Disallow deleting tables with snapshot schedules.
      auto covering_schedules = VERIFY_RESULT(
          master_->snapshot_coordinator().GetSnapshotSchedules(
              SysRowEntryType::TABLE, table_id));
      if (!covering_schedules.empty()) {
        return STATUS_EC_FORMAT(
            NotSupported,
            MasterError(MasterErrorPB::INVALID_REQUEST),
            "Cannot truncate table $0 which has schedule: $1",
            table->name(),
            AsString(covering_schedules));
      }
  }

  SCHECK_EC_FORMAT(
      FLAGS_enable_delete_truncate_xcluster_replicated_table ||
          !xcluster_manager_->IsTableReplicated(table_id),
      NotSupported, MasterError(MasterErrorPB::INVALID_REQUEST),
      "Cannot truncate table $0 that is in xCluster replication", table_id);

  {
    SharedLock lock(mutex_);
    SCHECK_EC_FORMAT(
        FLAGS_enable_truncate_cdcsdk_table || !IsTablePartOfCDCSDK(table_id), NotSupported,
        MasterError(MasterErrorPB::INVALID_REQUEST),
        "Cannot truncate a table $0 that has a CDCSDK Stream", table_id);
  }

  // Send a Truncate() request to each tablet in the table.
  SendTruncateTableRequest(table, epoch);

  LOG(INFO) << "Successfully initiated TRUNCATE for " << table->ToString() << " per request from "
            << RequestorString(rpc);
  background_tasks_->Wake();

  // Truncate indexes also.  For YSQL, truncate for each index is sent separately in
  // YBCTruncateTable, so don't handle it here.  Also, it would be incorrect to handle it here in
  // case the index is part of a tablegroup.
  if (table->GetTableType() != PGSQL_TABLE_TYPE) {
    const bool is_index = IsIndex(l->pb);
    DCHECK(!is_index || l->pb.indexes().empty()) << "indexes should be empty for index table";
    for (const auto& index_info : l->pb.indexes()) {
      RETURN_NOT_OK(TruncateTable(index_info.table_id(), resp, rpc, epoch));
    }
  }

  return Status::OK();
}

void CatalogManager::SendTruncateTableRequest(
    const scoped_refptr<TableInfo>& table, const LeaderEpoch& epoch) {
  auto tablets_result = table->GetTablets();
  if (!tablets_result.ok()) {
    WARN_NOT_OK(tablets_result.status(),
                Format("Failed to sent truncate table request for table $0", table->id()));
    return;
  }
  for (const auto& tablet : *tablets_result) {
    SendTruncateTabletRequest(tablet, epoch);
  }
}

void CatalogManager::SendTruncateTabletRequest(
    const TabletInfoPtr& tablet, const LeaderEpoch& epoch) {
  LOG_WITH_PREFIX(INFO) << "Truncating tablet " << tablet->id();
  auto call = std::make_shared<AsyncTruncate>(master_, AsyncTaskPool(), tablet, epoch);
  tablet->table()->AddTask(call);
  WARN_NOT_OK(
      ScheduleTask(call),
      Substitute("Failed to send truncate request for tablet $0", tablet->id()));
}

Status CatalogManager::IsTruncateTableDone(const IsTruncateTableDoneRequestPB* req,
                                           IsTruncateTableDoneResponsePB* resp) {
  LOG(INFO) << "Servicing IsTruncateTableDone request for table id " << req->table_id();

  // Lookup the truncated table.
  TRACE("Looking up table $0", req->table_id());
  scoped_refptr<TableInfo> table;
  {
    SharedLock lock(mutex_);
    table = tables_->FindTableOrNull(req->table_id());
  }

  if (table == nullptr) {
    Status s = STATUS(NotFound, "The object does not exist: table with id", req->table_id());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  TRACE("Locking table");
  RETURN_NOT_OK(
      CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(table->LockForRead(), resp));

  resp->set_done(!table->HasTasks(server::MonitoredTaskType::kTruncateTablet));
  return Status::OK();
}

// Note: only used by YSQL as of 2020-10-29.
Status CatalogManager::BackfillIndex(
    const BackfillIndexRequestPB* req,
    BackfillIndexResponsePB* resp,
    rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  // We don't expect to be called for index backfill during a ysql major catalog upgrade.
  RSTATUS_DCHECK(
      !IsYsqlMajorCatalogUpgradeInProgress(), InternalError,
      "Attempting to backfill index during ysql major catalog upgrade");
  const TableIdentifierPB& index_table_identifier = req->index_identifier();

  scoped_refptr<TableInfo> index_table = VERIFY_RESULT(FindTable(index_table_identifier));

  if (index_table->GetTableType() != PGSQL_TABLE_TYPE) {
    // This request is only supported for YSQL for now.  YCQL has its own mechanism.
    return STATUS(
        InvalidArgument,
        "Unexpected non-YSQL table",
        index_table_identifier.ShortDebugString());
  }

  // Collect indexed_table.
  scoped_refptr<TableInfo> indexed_table;
  {
    auto l = index_table->LockForRead();
    TableId indexed_table_id = GetIndexedTableId(l->pb);
    resp->mutable_table_identifier()->set_table_id(indexed_table_id);
    indexed_table = GetTableInfo(indexed_table_id);
  }

  if (indexed_table == nullptr) {
    return STATUS(InvalidArgument, "Empty indexed table",
                  index_table_identifier.ShortDebugString());
  }

  // TODO(jason): when ready to use INDEX_PERM_DO_BACKFILL for resuming backfill across master
  // leader changes, replace the following (issue #6218).

  // Collect index_info_pb.
  IndexInfoPB index_info_pb;
  indexed_table->GetIndexInfo(index_table->id()).ToPB(&index_info_pb);
  if (index_info_pb.index_permissions() != INDEX_PERM_WRITE_AND_DELETE) {
    return SetupError(
        resp->mutable_error(),
        MasterErrorPB::INVALID_SCHEMA,
        STATUS_FORMAT(
            InvalidArgument,
            "Expected WRITE_AND_DELETE perm, got $0",
            IndexPermissions_Name(index_info_pb.index_permissions())));
  }

  return MultiStageAlterTable::StartBackfillingData(
      this, indexed_table, {index_info_pb}, boost::none, epoch);
}

Status CatalogManager::GetBackfillJobs(
    const GetBackfillJobsRequestPB* req,
    GetBackfillJobsResponsePB* resp,
    rpc::RpcContext* rpc) {
  TableIdentifierPB table_id = req->table_identifier();

  scoped_refptr<TableInfo> indexed_table = VERIFY_RESULT(FindTable(table_id));
  if (indexed_table == nullptr) {
    Status s = STATUS(NotFound, "Requested table $0 does not exist", table_id.ShortDebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  {
    auto l = indexed_table->LockForRead();
    resp->mutable_backfill_jobs()->CopyFrom(l->pb.backfill_jobs());
  }
  return Status::OK();
}

void CatalogManager::GetBackfillStatus(
    const TableId& indexed_table_id, TableIdSet&& indexes, auto&& callback) {
  CHECK(!indexed_table_id.empty());

  // Utility functor to respond with the provided error for the remaining indexes.
  auto callback_failure = [&callback, &indexes](const Status& status) {
    for (const auto& index : indexes) {
      callback(status, index, IndexStatusPB::BACKFILL_UNKNOWN);
    }
  };

  auto indexed_table = GetTableInfo(indexed_table_id);
  if (!indexed_table) {
    callback_failure(STATUS_FORMAT(NotFound, "Indexed table $0 is not found", indexed_table));
    return;
  }

  const bool filter_by_index = !indexes.empty();
  auto indexed_table_lock = indexed_table->LockForRead();
  for (const auto& index_info_pb : indexed_table_lock->pb.indexes()) {
    if (filter_by_index) {
      if (indexes.empty()) {
        break; // Iterated through all the requested indexes, no need to continue.
      }

      auto index_it = indexes.find(index_info_pb.table_id());
      if (index_it == indexes.cend()) {
        continue; // Not interested in the current index.
      }

      // Need to erase from the map to be able to track not found indexes.
      indexes.erase(index_it);
    }

    VLOG_WITH_PREFIX_AND_FUNC(1) << Format(
        "Index table $0 permissions: [$1]",
        index_info_pb.table_id(),
        index_info_pb.has_index_permissions() ?
            IndexPermissions_Name(index_info_pb.index_permissions()) : "-");

    callback(Status::OK(), index_info_pb.table_id(), master::GetBackfillStatus(index_info_pb));
  }

  // Nofify the caller with the remaining indexes. There's a chance some of the indexes
  // have been removed right before locking the indexed table.
  if (!indexes.empty()) {
    callback_failure(STATUS(NotFound, "Index table is not found"));
  }
}

Status CatalogManager::GetBackfillStatus(
    const GetBackfillStatusRequestPB* req,
    GetBackfillStatusResponsePB* resp,
    rpc::RpcContext*) {
  VLOG_WITH_PREFIX_AND_FUNC(1) << "request: " << req->ShortDebugString();

  // The caller expects results for every specified table.
  resp->mutable_index_status()->Reserve(req->index_tables_size());

  // First step is to group all incoming index tables by indexed table id to lock that particular
  // indexed table only once. Also it is required to keep input table identifiers to re-use them
  // in the response as is.
  std::unordered_map<TableId, TableIdSet> indexed_table_to_indexes_map;
  std::unordered_map<TableId, const TableIdentifierPB*> index_to_identifier_map;
  for (const auto& index_table_pb : req->index_tables()) {
    Status status = Status::OK();
    auto result = FindTable(index_table_pb);
    if (!result.ok()) {
      status = result.status();
    } else {
      const auto& table_info = *result;
      const auto indexed_table_id = table_info->indexed_table_id();
      if (indexed_table_id.empty()) {
        status = STATUS_FORMAT(InvalidArgument,
            "Table $0 is not an index table", index_table_pb.ShortDebugString());
      } else {
        indexed_table_to_indexes_map[indexed_table_id].emplace(table_info->id());
        index_to_identifier_map.emplace(table_info->id(), &index_table_pb);
      }
    }

    if (!status.ok()) {
      auto* index_status = resp->add_index_status();
      index_status->mutable_index_table()->CopyFrom(index_table_pb);
      StatusToPB(status, index_status->mutable_error());
      LOG_WITH_PREFIX_AND_FUNC(INFO) << "Failed to get index status for table "
          << index_table_pb.ShortDebugString() << ", status: " << status;
    }
  }

  // Second step is to iterate over indexed_table_map and get the statuses for all required indexes.
  for (auto& [table_id, indexes] : indexed_table_to_indexes_map) {
    if (indexes.empty()) {
      LOG_WITH_PREFIX(DFATAL) << "No index tables are specified for table " << table_id;
      continue;
    }

    GetBackfillStatus(
        table_id, std::move(indexes),
        [&index_to_identifier_map, &resp](
            const Status& status, const TableId& index_id,
            IndexStatusPB::BackfillStatus backfill_status) {
          auto identifier_it = index_to_identifier_map.find(index_id);
          CHECK(identifier_it != index_to_identifier_map.end());
          auto* index_status = resp->add_index_status();
          index_status->mutable_index_table()->CopyFrom(*identifier_it->second);
          if (status.ok()) {
            index_status->set_backfill_status(backfill_status);
          } else {
             StatusToPB(status, index_status->mutable_error());
          }
        });
  }

  return Status::OK();
}

Status CatalogManager::LaunchBackfillIndexForTable(
    const LaunchBackfillIndexForTableRequestPB* req,
    LaunchBackfillIndexForTableResponsePB* resp,
    rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  const TableIdentifierPB& table_id = req->table_identifier();

  scoped_refptr<TableInfo> indexed_table = VERIFY_RESULT(FindTable(table_id));
  if (indexed_table == nullptr) {
    Status s = STATUS(NotFound, "Requested table $0 does not exist", table_id.ShortDebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }
  if (indexed_table->GetTableType() != YQL_TABLE_TYPE) {
    // This request is only supported for YCQL for now.  YSQL has its own mechanism.
    return STATUS(InvalidArgument, "Unexpected non-YCQL table $0", table_id.ShortDebugString());
  }

  uint32_t current_version;
  {
    auto l = indexed_table->LockForRead();
    if (l->pb.state() != SysTablesEntryPB::RUNNING) {
      Status s = STATUS(TryAgain,
                        "The table is in state $0. An alter may already be in progress.",
                        SysTablesEntryPB_State_Name(l->pb.state()));
      VLOG(2) << "Table " << indexed_table->ToString() << " is not running returning " << s;
      return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, s);
    }
    current_version = l->pb.version();
  }

  auto s = MultiStageAlterTable::LaunchNextTableInfoVersionIfNecessary(
      this, indexed_table, current_version, epoch, /* respect deferrals for backfill */ false);
  if (!s.ok()) {
    VLOG(3) << __func__ << " Done failed " << s;
    return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, s);
  }
  return Status::OK();
}

Status CatalogManager::MarkIndexInfoFromTableForDeletion(
    const TableId& indexed_table_id, const TableId& index_table_id, bool multi_stage,
    const LeaderEpoch& epoch,
    DeleteTableResponsePB* resp,
    std::map<TableId, DeletingTableData>* data_map_ptr) {
  LOG(INFO) << "MarkIndexInfoFromTableForDeletion table " << indexed_table_id
            << " index " << index_table_id << " multi_stage=" << multi_stage;
  // Lookup the indexed table and verify if it exists.
  scoped_refptr<TableInfo> indexed_table = GetTableInfo(indexed_table_id);
  if (indexed_table == nullptr) {
    LOG(WARNING) << "Indexed table " << indexed_table_id << " for index "
                 << index_table_id << " not found";
    return Status::OK();
  }

  if (resp) {
    auto ns_info = VERIFY_RESULT(master_->catalog_manager()->FindNamespaceById(
        indexed_table->namespace_id()));
    auto* resp_indexed_table = resp->mutable_indexed_table();
    resp_indexed_table->mutable_namespace_()->set_name(ns_info->name());
    resp_indexed_table->mutable_namespace_()->set_id(ns_info->id());
    resp_indexed_table->set_table_name(indexed_table->name());
    resp_indexed_table->set_table_id(indexed_table_id);
  }
  if (multi_stage) {
    RSTATUS_DCHECK(!data_map_ptr, InvalidArgument, "data_map_ptr is set");
    RETURN_NOT_OK(MultiStageAlterTable::UpdateIndexPermission(
        this, indexed_table,
        {{index_table_id, IndexPermissions::INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING}}, epoch));
  } else {
    RSTATUS_DCHECK(data_map_ptr, InvalidArgument, "data_map_ptr is not set");
    RETURN_NOT_OK(DeleteIndexInfoFromTable(indexed_table_id, index_table_id, epoch, data_map_ptr));
  }

  // Actual Deletion of the index info will happen asynchronously after all the
  // tablets move to the new IndexPermission of DELETE_ONLY_WHILE_REMOVING.
  RETURN_NOT_OK(SendAlterTableRequest(indexed_table, epoch));
  return Status::OK();
}

Status CatalogManager::DeleteIndexInfoFromTable(
    const TableId& indexed_table_id, const TableId& index_table_id, const LeaderEpoch& epoch,
    std::map<TableId, DeletingTableData>* data_map_ptr) {
  LOG(INFO) << "DeleteIndexInfoFromTable table " << indexed_table_id << " index " << index_table_id;
  scoped_refptr<TableInfo> indexed_table = GetTableInfo(indexed_table_id);
  if (indexed_table == nullptr) {
    LOG(WARNING) << "Indexed table " << indexed_table_id << " for index " << index_table_id
                 << " not found";
    return Status::OK();
  }
  TRACE("Locking indexed table");
  TableInfo::WriteLock indexed_table_write_lock;
  TableInfo::WriteLock* l_ptr;
  if (data_map_ptr) {
    l_ptr = &(*data_map_ptr)[indexed_table_id].write_lock;
  } else {
    indexed_table_write_lock = indexed_table->LockForWrite();
    l_ptr = &indexed_table_write_lock;
  }
  auto& l = *l_ptr;
  auto &indexed_table_data = *l.mutable_data();

  // Heed issue #6233.
  if (!l->pb.has_fully_applied_schema()) {
    MultiStageAlterTable::CopySchemaDetailsToFullyApplied(&indexed_table_data.pb);
  }
  auto *indexes = indexed_table_data.pb.mutable_indexes();
  for (int i = 0; i < indexes->size(); i++) {
    if (indexes->Get(i).table_id() == index_table_id) {

      indexes->DeleteSubrange(i, 1);

      indexed_table_data.pb.set_version(indexed_table_data.pb.version() + 1);
      // TODO(Amit) : Is this compatible with the previous version?
      indexed_table_data.pb.set_updates_only_index_permissions(false);
      indexed_table_data.set_state(
          SysTablesEntryPB::ALTERING,
          Format("Delete index info version=$0 ts=$1",
                 indexed_table_data.pb.version(), LocalTimeAsString()));

      // Update sys-catalog with the deleted indexed table info.
      TRACE("Updating indexed table metadata on disk");
      RETURN_NOT_OK(sys_catalog_->Upsert(epoch, indexed_table));

      // Update the in-memory state.
      TRACE("Committing in-memory state");
      l.Commit();
      return Status::OK();
    }
  }

  LOG(WARNING) << "Index " << index_table_id << " not found in indexed table " << indexed_table_id;
  return Status::OK();
}

void CatalogManager::AcquireObjectLocksGlobal(
    const AcquireObjectLocksGlobalRequestPB* req, AcquireObjectLocksGlobalResponsePB* resp,
    rpc::RpcContext rpc) {
  VLOG(0) << __PRETTY_FUNCTION__;
  if (!FLAGS_TEST_enable_object_locking_for_table_locks) {
    rpc.RespondRpcFailure(
        rpc::ErrorStatusPB::ERROR_APPLICATION,
        STATUS(NotSupported, "Flag enable_object_locking_for_table_locks disabled"));
    return;
  }
  object_lock_info_manager_->LockObject(*req, resp, std::move(rpc));
}

void CatalogManager::ReleaseObjectLocksGlobal(
    const ReleaseObjectLocksGlobalRequestPB* req, ReleaseObjectLocksGlobalResponsePB* resp,
    rpc::RpcContext rpc) {
  VLOG(0) << __PRETTY_FUNCTION__;
  if (!FLAGS_TEST_enable_object_locking_for_table_locks) {
    rpc.RespondRpcFailure(
        rpc::ErrorStatusPB::ERROR_APPLICATION,
        STATUS(NotSupported, "Flag enable_object_locking_for_table_locks disabled"));
    return;
  }
  object_lock_info_manager_->UnlockObject(*req, resp, std::move(rpc));
}

void CatalogManager::ExportObjectLockInfo(
    const std::string& tserver_uuid, tserver::DdlLockEntriesPB* resp) {
  object_lock_info_manager_->ExportObjectLockInfo(tserver_uuid, resp);
}

Status CatalogManager::GetIndexBackfillProgress(const GetIndexBackfillProgressRequestPB* req,
                                                GetIndexBackfillProgressResponsePB* resp,
                                                rpc::RpcContext* rpc) {
  VLOG(1) << "Get Index Backfill Progress request " << req->ShortDebugString();

  // Note: the caller expects the ordering of the indexes in the response PB to be the
  // same as that in the request PB.
  for (const auto& index_id : req->index_ids()) {
    // Retrieve the number of rows processed by the backfill job.
    // When the backfill job is live, the num_rows_processed field in the indexed table's
    // BackfillJobPB would give us the desired information. For backfill jobs that haven't been
    // created or have completed/failed (and been cleared) the num_rows_processed field in the
    // IndexInfoPB would give us the desired information.
    // The following cases are possible:
    // 1) The index or the indexed table is not found: the information can't be determined so
    //    we set the num_rows_processed to 0.
    // 2) The backfill job hasn't been created: we use IndexInfoPB's num_rows_processed (in this
    //    case the value of this field is the default value 0).
    // 3) The backfill job is live: we use BackfillJobPB's num_rows_processed.
    // 4) The backfill job was successful/unsuccessful and cleared after completion:
    //    we use IndexInfoPB's num_rows_processed.
    // Notes:
    // a) We are safe from concurrency issues when reading the backfill state and
    // the index info's num_rows_processed because the updation for these two fields happens
    // within the same LockForWrite in BackfillTable::MarkIndexesAsDesired.
    auto index_table = GetTableInfo(index_id);
    if (index_table == nullptr) {
      LOG_WITH_FUNC(INFO) << "Requested Index " << index_id << " not found";
      // No backfill job can be found for this index - set the rows processed to 0.
      resp->add_rows_processed_entries(0);
      continue;
    }

    // Get indexed table's TableInfo.
    auto indexed_table = GetTableInfo(index_table->indexed_table_id());
    if (indexed_table == nullptr) {
      LOG_WITH_FUNC(INFO) << "Indexed table for requested index " << index_id << " not found";
      // No backfill job can be found for this index - set the rows processed to 0.
      resp->add_rows_processed_entries(0);
      continue;
    }

    auto l = indexed_table->LockForRead();
    if (l->pb.backfill_jobs_size() >= 1) {
      // Currently we do not support multiple backfill jobs, so there can only be one outstanding
      // backfill job. So the first (and only) backfill job at the 0th index is the only
      // live job we need to look at.
      DCHECK_EQ(l->pb.backfill_jobs_size(), 1) << "Expected 1 in-progress backfill job. "
          "Found: " << l->pb.backfill_jobs_size();
      // Check if the desired index is being backfilled by the live backfill job.
      const auto& backfill_job = l->pb.backfill_jobs(0);
      if (backfill_job.backfill_state().find(index_id) != backfill_job.backfill_state().end()) {
        resp->add_rows_processed_entries(backfill_job.num_rows_processed());
        continue;
      }
    }
    // Find the desired index's IndexInfoPB from the indexed table's TableInfo.
    for (const auto& index_info_pb : l->pb.indexes()) {
      if (index_info_pb.table_id() == index_id) {
        resp->add_rows_processed_entries(index_info_pb.num_rows_processed_by_backfill_job());
        break;
      }
    }
  }

  return Status::OK();
}

Status CatalogManager::ScheduleDeleteTable(
    const scoped_refptr<TableInfo>& table, const LeaderEpoch& epoch) {
  return background_tasks_thread_pool_->SubmitFunc([this, table, epoch]() {
    DeleteTableRequestPB del_tbl_req;
    DeleteTableResponsePB del_tbl_resp;
    del_tbl_req.mutable_table()->set_table_name(table->name());
    del_tbl_req.mutable_table()->set_table_id(table->id());

    WARN_NOT_OK(DeleteTable(&del_tbl_req, &del_tbl_resp, nullptr, epoch), "Failed to delete table");
  });
}

Status CatalogManager::DeleteTable(
    const DeleteTableRequestPB* req, DeleteTableResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing DeleteTable request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTable(req->table()));

  if (PREDICT_FALSE(FLAGS_TEST_keep_docdb_table_on_ysql_drop_table) &&
      table->GetTableType() == PGSQL_TABLE_TYPE) {
    return Status::OK();
  }

  if (table->IgnoreHideRequest()) {
    return Status::OK();
  }

  // For now, only disable dropping YCQL tables under xCluster replication.
  bool result =
      table->GetTableType() == YQL_TABLE_TYPE && xcluster_manager_->IsTableReplicated(table->id());
  if (!FLAGS_enable_delete_truncate_xcluster_replicated_table && result) {
    return STATUS(NotSupported,
                  "Cannot delete a table in replication.",
                  req->ShortDebugString(),
                  MasterError(MasterErrorPB::INVALID_REQUEST));
  }

  if (req->ysql_yb_ddl_rollback_enabled()) {
    DCHECK(req->has_transaction());
    DCHECK(req->transaction().has_transaction_id());
  }

  scoped_refptr<TableInfo> indexed_table;
  if (req->is_index_table()) {
    TRACE("Looking up index");
    TableId table_id = table->id();
    resp->set_table_id(table_id);
    TableId indexed_table_id;
    bool is_transactional = false;
    TableType index_table_type;
    {
      auto l = table->LockForRead();
      indexed_table_id = GetIndexedTableId(l->pb);
      is_transactional = l->schema().table_properties().is_transactional();
      index_table_type = l->table_type();
    }
    indexed_table = GetTableInfo(indexed_table_id);
    const bool is_pg_table = indexed_table != nullptr &&
                             indexed_table->GetTableType() == PGSQL_TABLE_TYPE;
    if (!is_pg_table && IsIndexBackfillEnabled(index_table_type, is_transactional)) {
      return MarkIndexInfoFromTableForDeletion(
          indexed_table_id, table_id, /* multi_stage */ true, epoch, resp, nullptr);
    }

    // If DDL Rollback is enabled, we will not delete the index now, but merely mark it for
    // deletion when the transaction commits. Thus set the response fields required by the client
    // right away.
    if (is_pg_table && req->ysql_yb_ddl_rollback_enabled()) {
      auto ns_info = VERIFY_RESULT(master_->catalog_manager()->FindNamespaceById(
          indexed_table->namespace_id()));
      auto* resp_indexed_table = resp->mutable_indexed_table();
      resp_indexed_table->mutable_namespace_()->set_name(ns_info->name());
      resp_indexed_table->mutable_namespace_()->set_id(ns_info->id());
      resp_indexed_table->set_table_name(indexed_table->name());
      resp_indexed_table->set_table_id(indexed_table_id);
    }
  }

  // Check whether DDL rollback is enabled.
  if (req->ysql_yb_ddl_rollback_enabled() && req->has_transaction() &&
      table->GetTableType() == PGSQL_TABLE_TYPE) {
    bool ysql_txn_verifier_state_present = false;
    auto l = table->LockForWrite();
    if (l->has_ysql_ddl_txn_verifier_state()) {
      DCHECK(!l->pb_transaction_id().empty());

      // If the table is already undergoing DDL transaction verification as part of a different
      // transaction then fail this request.
      if (l->pb_transaction_id() != req->transaction().transaction_id()) {
        auto txn_meta = VERIFY_RESULT(TransactionMetadata::FromPB(l->pb.transaction()));
        auto req_txn_meta = VERIFY_RESULT(TransactionMetadata::FromPB(req->transaction()));
        RETURN_NOT_OK(TriggerDdlVerificationIfNeeded(txn_meta, epoch));
        return STATUS_EC_FORMAT(TryAgain,
            MasterError(MasterErrorPB::TABLE_SCHEMA_CHANGE_IN_PROGRESS),
            "DDL transaction $0 cannot continue as table $1 is undergoing DDL transaction "
            "verification for $2", req_txn_meta.transaction_id, table->name(),
            txn_meta.transaction_id);
      }
      // This DROP operation is part of a DDL transaction that has already made changes
      // to this table.
      ysql_txn_verifier_state_present = true;
    }

    // Setup a background task. It monitors the YSQL transaction. If it commits, the task drops
    // the table. Otherwise it removes the deletion marker in the ysql_ddl_txn_verifier_state.
    // Note that we could ideally drop this table right now if it was created in the same
    // transaction, as we are sure that this table will be dropped whether the transaction commits
    // or aborts. However, in cases where a table could be deleted and re-created multiple times in
    // an alter operation, that would abort the DDL transaction operating on it. Hence we do not
    // perform the actual deletion until commit.
    TransactionMetadata txn;
    auto& pb = l.mutable_data()->pb;
    if (!ysql_txn_verifier_state_present) {
      pb.mutable_transaction()->CopyFrom(req->transaction());
      txn = VERIFY_RESULT(TransactionMetadata::FromPB(req->transaction()));
      RSTATUS_DCHECK(!txn.status_tablet.empty(), Corruption, "Given incomplete Transaction");
      pb.add_ysql_ddl_txn_verifier_state();
    }
    DCHECK_EQ(pb.ysql_ddl_txn_verifier_state_size(), 1);
    pb.mutable_ysql_ddl_txn_verifier_state(0)->set_contains_drop_table_op(true);
    // Upsert to sys_catalog.
    RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table));
    // Update the in-memory state.
    TRACE("Committing in-memory state as part of DeleteTable operation");
    l.Commit();
    if (!ysql_txn_verifier_state_present) {
      RETURN_NOT_OK(ScheduleYsqlTxnVerification(table, txn, epoch));
    }
    return Status::OK();
  }

  return DeleteTableInternal(req, resp, rpc, epoch);
}

// Delete a Table
//  - Update the table state to "DELETING".
//  - Issue DeleteTablet tasks to all said tablets.
//  - Update all the underlying tablet states as "DELETED".
//
// This order of events can help us guarantee that:
//  - If a table is DELETING/DELETED, we do not add further tasks to it.
//  - A DeleteTable is done when a table is either DELETING or DELETED and has no running tasks.
//  - If a table is DELETING and it has no tasks on it, then it is safe to mark DELETED.
//
// We are lazy about deletions.
//
// IMPORTANT: If modifying, consider updating DeleteYsqlDBTables(), the bulk deletion API.
Status CatalogManager::DeleteTableInternal(
    const DeleteTableRequestPB* req, DeleteTableResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTable(req->table()));

  if (table->IgnoreHideRequest()) {
    return Status::OK();
  }

  auto schedules_to_tables_map = VERIFY_RESULT(
      master_->snapshot_coordinator().MakeSnapshotSchedulesToObjectIdsMap(SysRowEntryType::TABLE));

  vector<DeletingTableData> tables;
  RETURN_NOT_OK(DeleteTableInMemory(req->table(), req->is_index_table(),
                                    true /* update_indexed_table */, schedules_to_tables_map, epoch,
                                    &tables, resp, rpc, nullptr));

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  std::unordered_set<TableId> sys_table_ids;
  std::unordered_set<TableId> deleted_table_ids;
  for (auto& table : tables) {
    deleted_table_ids.insert(table.table_info->id());
    if (table.table_info->is_system()) {
      sys_table_ids.insert(table.table_info->id());
    }
    table.write_lock.Commit();
  }

  bool TEST_fail = false;
  TEST_SYNC_POINT_CALLBACK("DeleteTableInternal::FailAfterTableMarkedInSysCatalog", &TEST_fail);
  SCHECK(!TEST_fail, IllegalState, "Failing for TESTING");

  // table_id for the requested table will be added to the end of the response.
  RSTATUS_DCHECK_GE(resp->deleted_table_ids_size(), 1, IllegalState,
      "DeleteTableInMemory expected to add the index id to resp");

  RETURN_NOT_OK(xcluster_manager_->RemoveDroppedTablesOnConsumer(deleted_table_ids, epoch));

  if (PREDICT_FALSE(FLAGS_catalog_manager_inject_latency_in_delete_table_ms > 0)) {
    LOG(INFO) << "Sleeping in CatalogManager::DeleteTable for " <<
        FLAGS_catalog_manager_inject_latency_in_delete_table_ms << " ms";
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_catalog_manager_inject_latency_in_delete_table_ms));
  }

  // Update the internal table maps. Exclude Postgres tables which are not in the name map.
  // Also exclude hidden tables, that were already removed from this map.
  if (std::any_of(tables.begin(), tables.end(), [](auto& t) { return t.remove_from_name_map; })) {
    TRACE("Removing tables from by-name map");
    vector<TableId> removed_ycql_tables;
    {
      LockGuard lock(mutex_);
      for (const auto& table : tables) {
        if (table.remove_from_name_map) {
          TableInfoByNameMap::key_type key = {
              table.table_info->namespace_id(), table.table_info->name()};
          if (table_names_map_.erase(key) != 1) {
            LOG(WARNING) << "Could not remove table from map: " << key.first << "." << key.second;
          }

          // Keep track of deleted ycql tables.
          if (IsYcqlTable(*table.table_info)) {
            removed_ycql_tables.push_back(table.table_info->id());
          }
        }
      }
      // We commit another map to increment its version and reset cache.
      // Since table_name_map_ does not have version.
      tables_.Commit();
    }
    if (!removed_ycql_tables.empty()) {
      // Also remove from the system.partitions table.
      GetYqlPartitionsVtable().RemoveFromCache(removed_ycql_tables);
    }
  }

  for (const auto& table : tables) {
    LOG(INFO) << "Deleting table: " << table.ToString();

    // Send a DeleteTablet() request to each tablet replica in the table.
    RETURN_NOT_OK(DeleteOrHideTabletsOfTable(table.table_info, table.delete_retainer, epoch));
    auto colocated_tablet = table.table_info->GetColocatedUserTablet();
    if (colocated_tablet) {
      // TryRemoveFromTablegroup only affects tables that are part of some tablegroup.
      // We directly remove it from tablegroup no matter if it is retained by snapshot schedules.
      RETURN_NOT_OK(TryRemoveFromTablegroup(table.table_info->id()));
      // Send a RemoveTableFromTablet() request to each
      // colocated parent tablet replica in the table.
      if (!table.delete_retainer.IsHideOnly()) {
        LOG(INFO) << "Notifying tablet with id " << colocated_tablet->tablet_id()
                  << " to remove this colocated table " << table.table_info->name()
                  << " from its metadata.";
        auto call = std::make_shared<AsyncRemoveTableFromTablet>(
            master_, AsyncTaskPool(), colocated_tablet, table.table_info, epoch);
        table.table_info->AddTask(call);
        WARN_NOT_OK(ScheduleTask(call), "Failed to send RemoveTableFromTablet request");
      } else {
        // Set the snapshot schedules that prevented the table from getting deleted.
        const auto retained_by_snapshot_schedules =
            table.delete_retainer.RetainedBySnapshotSchedules();
        if (!retained_by_snapshot_schedules.empty()) {
          auto tablet_lock = colocated_tablet->LockForWrite();

          *tablet_lock.mutable_data()->pb.mutable_retained_by_snapshot_schedules() =
              retained_by_snapshot_schedules;

          // Upsert to sys catalog and commit to memory.
          RETURN_NOT_OK(sys_catalog_->Upsert(epoch, colocated_tablet));
          tablet_lock.Commit();
        }
        CheckTableDeleted(table.table_info, epoch);
      }
    }
  }

  RETURN_NOT_OK(DropCDCSDKStreams(deleted_table_ids));

  // If there are any permissions granted on this table find them and delete them. This is necessary
  // because we keep track of the permissions based on the canonical resource name which is a
  // combination of the keyspace and table names, so if another table with the same name is created
  // (in the same keyspace where the previous one existed), and the permissions were not deleted at
  // the time of the previous table deletion, then the permissions that existed for the previous
  // table will automatically be granted to the new table even though this wasn't the intention.
  string canonical_resource = get_canonical_table(req->table().namespace_().name(),
                                                  req->table().table_name());
  RETURN_NOT_OK(permissions_manager_->RemoveAllPermissionsForResource(canonical_resource, resp));

  // Remove the system tables from system catalog.
  if (!sys_table_ids.empty()) {
    // We do not expect system tables deletion during initial snapshot forming.
    DCHECK(!initial_snapshot_writer_);

    TRACE("Sending system table delete RPCs");
    for (auto& table_id : sys_table_ids) {
      // "sys_catalog_->DeleteYsqlSystemTable(table_id)" won't work here
      // as it only acts on the leader.
      tablet::ChangeMetadataRequestPB change_req;
      change_req.set_tablet_id(kSysCatalogTabletId);
      change_req.set_remove_table_id(table_id);
      RETURN_NOT_OK(tablet::SyncReplicateChangeMetadataOperation(
          &change_req, sys_catalog_->tablet_peer().get(), leader_ready_term()));
    }
  } else {
    TRACE("No system tables to delete");
  }

  LOG(INFO) << "Successfully initiated deletion of "
            << (req->is_index_table() ? "index" : "table") << " with "
            << req->table().DebugString() << " per request from " << RequestorString(rpc);
  // Asynchronously cleans up the final memory traces of the deleted database.
  background_tasks_->Wake();
  return Status::OK();
}

Status CatalogManager::DeleteTableInMemoryAcquireLocks(
    const scoped_refptr<master::TableInfo>& table,
    bool is_index_table,
    bool update_indexed_table,
    std::map<TableId, DeletingTableData>* data_map) {
  data_map->emplace(table->id(), DeletingTableData{.table_info = table});
  {
    auto l = table->LockForRead();
    if (is_index_table) {
      auto indexed_table_id = GetIndexedTableId(l->pb);
      auto indexed_table = GetTableInfo(indexed_table_id);
      if (indexed_table && update_indexed_table) {
        // We only need to lock indexed_table when we need to update it to
        // indicate that this index is gone.
        data_map->emplace(indexed_table_id, DeletingTableData{.table_info = indexed_table});
      }
    } else {
      // For regular table, we need to lock all of its indexes.
      TableIdentifierPB index_identifier;
      for (const auto& index : l->pb.indexes()) {
        index_identifier.set_table_id(index.table_id());
        auto index_result = FindTable(index_identifier);
        if (VERIFY_RESULT(DoesTableExist(index_result))) {
          auto index_table = std::move(*index_result);
          data_map->emplace(index.table_id(), DeletingTableData{.table_info = index_table});
        }
      }
    }
  }
  // Lock the table and indexes in the order of their table_ids.
  for (auto& it : *data_map) {
    it.second.write_lock = it.second.table_info->LockForWrite();
  }
  return Status::OK();
}

Status CatalogManager::DeleteTableInMemory(
    const TableIdentifierPB& table_identifier, const bool is_index_table,
    const bool update_indexed_table, const SnapshotSchedulesToObjectIdsMap& schedules_to_tables_map,
    const LeaderEpoch& epoch, vector<DeletingTableData>* tables, DeleteTableResponsePB* resp,
    rpc::RpcContext* rpc,
    std::map<TableId, DeletingTableData>* data_map_ptr) {
  // TODO(NIC): How to handle a DeleteTable request when the namespace is being deleted?
  const char* const object_type = is_index_table ? "index" : "table";
  const bool cascade_delete_index = is_index_table && !update_indexed_table;

  VLOG_WITH_PREFIX_AND_FUNC(1) << YB_STRUCT_TO_STRING(
      table_identifier, is_index_table, update_indexed_table) << "\n" << GetStackTrace();

  // Lookup the table and verify if it exists.
  TRACE(Substitute("Looking up $0", object_type));
  auto table_result = FindTable(table_identifier);
  if (!VERIFY_RESULT(DoesTableExist(table_result))) {
    if (cascade_delete_index) {
      LOG(WARNING) << "Index " << table_identifier.DebugString() << " not found";
      return Status::OK();
    } else {
      return table_result.status();
    }
  }
  auto table = std::move(*table_result);

  if (table->GetTableType() == TRANSACTION_STATUS_TABLE_TYPE) {
    {
      LockGuard lock(mutex_);
      transaction_table_ids_set_.erase(table->id());
    }
    RETURN_NOT_OK(IncrementTransactionTablesVersion());
    RETURN_NOT_OK(WaitForTransactionTableVersionUpdateToPropagate());
  }

  std::map<TableId, DeletingTableData> data_map;
  if (!data_map_ptr) {
    TRACE(Substitute("Locking $0", object_type));
    RETURN_NOT_OK(
        DeleteTableInMemoryAcquireLocks(table, is_index_table, update_indexed_table, &data_map));
    data_map_ptr = &data_map;
  }
  auto& data = (*data_map_ptr)[table->id()];
  auto& l = data.write_lock;
  // table_id for the requested table will be added to the end of the response.
  *resp->add_deleted_table_ids() = table->id();

  if (is_index_table == IsTable(l->pb)) {
    Status s = STATUS(NotFound, "The object does not exist");
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
  }

  data.delete_retainer =
      VERIFY_RESULT(GetDeleteRetainerInfoForTableDrop(*table, schedules_to_tables_map));

  bool hide_only = data.delete_retainer.IsHideOnly();

  if (l->started_deleting() || (hide_only && l->started_hiding())) {
    if (cascade_delete_index) {
      LOG(WARNING) << "Index " << table_identifier.ShortDebugString() << " was "
                   << (l->started_deleting() ? "deleted" : "hidden");
      return Status::OK();
    }

    return STATUS_EC_FORMAT(
        NotFound, MasterError(MasterErrorPB::OBJECT_NOT_FOUND), "The object was deleted",
        l->pb.state_msg());
  }

  // Determine if we have to remove from the name map here before we change the table state.
  data.remove_from_name_map = l.data().table_type() != PGSQL_TABLE_TYPE && !l->started_hiding();

  TRACE("Updating metadata on disk");
  // Update the metadata for the on-disk state.
  if (hide_only) {
    l.mutable_data()->pb.set_hide_state(SysTablesEntryPB::HIDING);
  } else {
    l.mutable_data()->set_state(SysTablesEntryPB::DELETING,
                                 Substitute("Started deleting at $0", LocalTimeAsString()));
  }

  auto now = master_->clock()->Now();
  DdlLogEntry ddl_log_entry(now, table->id(), l->pb, "Drop");
  if (is_index_table) {
    const auto& indexed_table_id = GetIndexedTableId(l->pb);
    auto indexed_table = FindTableById(indexed_table_id);
    if (indexed_table.ok()) {
      const auto& lock = (*data_map_ptr)[indexed_table_id].write_lock;
      ddl_log_entry = DdlLogEntry(
          now, indexed_table_id, lock->pb, Format("Drop index $0", l->name()));
    }
  }

  // Update sys-catalog with the removed table state.
  Status s = sys_catalog_->Upsert(epoch, &ddl_log_entry, table);

  if (PREDICT_FALSE(FLAGS_TEST_simulate_crash_after_table_marked_deleting)) {
    return Status::OK();
  }

  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend("An error occurred while updating sys tables");
    LOG(WARNING) << s;
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  // For regular (indexed) table, delete all its index tables if any. Else for index table, delete
  // index info from the indexed table.
  if (!is_index_table) {
    TableIdentifierPB index_identifier;
    for (const auto& index : l->pb.indexes()) {
      index_identifier.set_table_id(index.table_id());
      RETURN_NOT_OK(DeleteTableInMemory(
          index_identifier, true /* is_index_table */, false /* update_indexed_table */,
          schedules_to_tables_map, epoch, tables, resp, rpc, data_map_ptr));
    }
  } else if (update_indexed_table) {
    auto indexed_table_id = GetIndexedTableId(l->pb);
    s = MarkIndexInfoFromTableForDeletion(
        indexed_table_id, table->id(), /* multi_stage */ false, epoch, resp, data_map_ptr);
    if (!s.ok()) {
      s = s.CloneAndPrepend(Substitute("An error occurred while deleting index info: $0",
                                       s.ToString()));
      LOG(WARNING) << s.ToString();
      return CheckIfNoLongerLeaderAndSetupError(s, resp);
    }
  }

  if (!hide_only) {
    // If table is being hidden we should not abort snapshot related tasks.
    // Always ignore Table schema verification tasks since it may be the one that is initiating the
    // deletes.
    table->AbortTasks(/*tasks_to_ignore=*/{server::MonitoredTaskType::TableSchemaVerification});
  }

  // For regular (indexed) table, insert table info and lock in the front of the list. Else for
  // index table, append them to the end. We do so so that we will commit and delete the indexed
  // table first before its indexes.
  tables->insert(is_index_table ? tables->end() : tables->begin(), std::move(data));

  return Status::OK();
}

bool CatalogManager::ShouldDeleteTable(const TableInfoPtr& table) {
  if (!table) {
    LOG_WITH_PREFIX(INFO) << "Finished deleting an Orphaned tablet. "
                          << "Table Information is null. Skipping updating its state to DELETED.";
    return false;
  }
  // Wait for all the replica delete tasks that were sent to all peers to complete.
  if (table->HasTasks(server::MonitoredTaskType::kDeleteReplica)) {
    VLOG_WITH_PREFIX_AND_FUNC(2) << table->ToString() << " has more replica delete tasks";
    return false;
  }
  bool hide_only;
  {
    auto lock = table->LockForRead();

    // For any table in DELETING state, we will want to mark it as DELETED once all its respective
    // tablets have been successfully removed from tservers.
    // For any hiding table we will want to mark it as HIDDEN once all its respective
    // tablets have been successfully hidden on tservers.
    if (lock->is_deleted()) {
      // Clear the tablets_ and partitions_ maps if table has already been DELETED.
      // Usually this would have been done except for tables that were hidden and are now deleted.
      // Also, this is a catch all in case any other path misses clearing the maps.
      table->ClearTabletMaps();
      return false;
    }
    hide_only = !lock->is_deleting();
    if (hide_only && !lock->is_hiding()) {
      return false;
    }
  }
  // The current relevant order of operations during a DeleteTable is:
  // 1) Mark the table as DELETING
  // 2) Abort the current table tasks
  // 3) Per tablet, send DeleteTable requests to all TS, then mark that tablet as DELETED
  //
  // This creates a race, wherein, after 2, HasTasks can be false, but we still have not
  // gotten to point 3, which would add further tasks for the deletes.
  //
  // However, HasTasks is cheaper than AreAllTabletsDeletedOrHidden...
  auto all_tablets_done_result =
      hide_only ? table->AreAllTabletsHidden() : table->AreAllTabletsDeleted();
  if (!all_tablets_done_result.ok()) {
    return false;
  }
  auto all_tablets_done = *all_tablets_done_result;
  VLOG_WITH_PREFIX_AND_FUNC(2)
      << table->ToString() << " hide only: " << hide_only << ", all tablets done: "
      << all_tablets_done;
  return all_tablets_done || table->is_system() || table->IsColocatedUserTable();
}

TableInfo::WriteLock CatalogManager::PrepareTableDeletion(const TableInfoPtr& table) {
  auto lock = table->LockForWrite();
  if (lock->is_hiding()) {
    LOG(INFO) << "Marking table as HIDDEN: " << table->ToString();
    lock.mutable_data()->pb.set_hide_state(SysTablesEntryPB::HIDDEN);
    lock.mutable_data()->pb.set_hide_hybrid_time(master_->clock()->Now().ToUint64());
    // Don't erase hidden tablets from partitions_ as they are needed for CLONE, PITR, SELECT AS-OF.
    return lock;
  }
  if (lock->is_deleting()) {
    // Update the metadata for the on-disk state.
    LOG(INFO) << "Marking table as DELETED: " << table->ToString();
    lock.mutable_data()->set_state(SysTablesEntryPB::DELETED,
        Substitute("Deleted with tablets at $0", LocalTimeAsString()));
    // Erase all the tablets from tablets_ and partitions_ structures.
    table->ClearTabletMaps();
    return lock;
  }
  return TableInfo::WriteLock();
}

void CatalogManager::CleanUpDeletedTables(const LeaderEpoch& epoch) {
  // TODO(bogdan): Cache tables being deleted to make this iterate only over those?
  vector<scoped_refptr<TableInfo>> tables_to_delete;
  // Garbage collecting.
  // Going through all tables under the global lock, copying them to not hold lock for too long.
  std::vector<scoped_refptr<TableInfo>> tables;
  {
    LockGuard lock(mutex_);
    for (const auto& table : tables_->GetAllTables()) {
      tables.push_back(table);
    }
  }
  // Mark the tables as DELETED and remove them from the in-memory maps.
  vector<TableInfo*> tables_to_update_on_disk;
  vector<TableInfo::WriteLock> table_locks;
  for (const auto& table : tables) {
    if (ShouldDeleteTable(table)) {
      auto lock = PrepareTableDeletion(table);
      if (lock.locked()) {
        table_locks.push_back(std::move(lock));
        tables_to_update_on_disk.push_back(table.get());
      }
    }
  }
  if (tables_to_update_on_disk.size() > 0) {
    Status s = sys_catalog_->Upsert(epoch, tables_to_update_on_disk);
    if (!s.ok()) {
      LOG(WARNING) << "Error marking tables as DELETED: " << s.ToString();
      return;
    }
    // Update the table in-memory info as DELETED after we've removed them from the maps.
    for (auto& lock : table_locks) {
      lock.Commit();
    }
    for (auto table : tables_to_update_on_disk) {
      // Clean up any DDL verification state that is waiting for this table to start deleting.
      auto res = table->LockForRead()->GetCurrentDdlTransactionId();
      WARN_NOT_OK(
          res, Format("Failed to get current DDL transaction for table $0", table->ToString()));
      if (!res.ok() || res.get() == TransactionId::Nil()) {
        continue;
      }
      VLOG(3) << "Cleanup deleted table " << table->id();
      RemoveDdlTransactionState(table->id(), {res.get()});
    }
    // TODO: Check if we want to delete the totally deleted table from the sys_catalog here.
    // TODO: SysCatalog::DeleteItem() if we've DELETED all user tables in a DELETING namespace.
    // TODO: Also properly handle namespace_ids_map_.erase(table->namespace_id())
  }
}

Status CatalogManager::IsDeleteTableDone(const IsDeleteTableDoneRequestPB* req,
                                         IsDeleteTableDoneResponsePB* resp) {
  // Lookup the deleted table.
  TRACE("Looking up table $0", req->table_id());
  scoped_refptr<TableInfo> table;
  {
    SharedLock lock(mutex_);
    table = tables_->FindTableOrNull(req->table_id());
  }

  if (table == nullptr) {
    LOG(INFO) << "Servicing IsDeleteTableDone request for table id "
              << req->table_id() << ": deleted (not found)";
    resp->set_done(true);
    return Status::OK();
  }

  TRACE("Locking table");
  auto l = table->LockForRead();

  if (l->is_index() && l->table_type() != PGSQL_TABLE_TYPE) {
    if (IsIndexBackfillEnabled(
            l->table_type(), l->schema().table_properties().is_transactional())) {
      auto indexed_table = GetTableInfo(GetIndexedTableId(l->pb));
      if (indexed_table != nullptr &&
          indexed_table->AttachedYCQLIndexDeletionInProgress(req->table_id())) {
        LOG(INFO) << "Servicing IsDeleteTableDone request for index id " << req->table_id()
                  << " with backfill: deleting in state " << l->state_name();
        resp->set_done(l->is_deleted() || l->is_hidden());
        return Status::OK();
      }
    }
  }

  if (!l->started_deleting() && !l->started_hiding()) {
    LOG(WARNING) << "Servicing IsDeleteTableDone request for table id " << req->table_id()
                 << ": NOT deleted in state " << l->state_name();
    Status s = STATUS(IllegalState, "The object was NOT deleted", l->pb.state_msg());
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  if (l->is_deleted() || l->is_hidden()) {
    LOG(INFO) << "Servicing IsDeleteTableDone request for table id "
              << req->table_id() << ": totally " << (l->is_hidden() ? "hidden" : "deleted");
    resp->set_done(true);
  } else {
    LOG(INFO) << "Servicing IsDeleteTableDone request for table id " << req->table_id()
              << ((!table->IsColocatedUserTable()) ? ": deleting tablets" : "");

    auto descs = master_->ts_manager()->GetAllDescriptors();
    for (auto& ts_desc : descs) {
      LOG(INFO) << "Deleting on " << ts_desc->permanent_uuid() << ": "
                << ts_desc->PendingTabletDeleteToString();
    }

    resp->set_done(false);
  }

  return Status::OK();
}

namespace {
// Given the SysTablesEntryPB for a table and 'col_name', return whether the column can be directly
// dropped or if it should be marked for deletion first and dropped later.
Result<bool> NeedTwoPhaseDeleteForColumn(const SysTablesEntryPB& pb, const string& col_name) {
  // Applicable only for YSQL tables.
  if (pb.table_type() != PGSQL_TABLE_TYPE) {
    return false;
  }

  // In general, mark the column for deletion first and drop it only after the transaction commits
  // to prevent data loss. However, if the table was created in the same transaction or if the
  // column was added in this transaction, then we can safely skip 2-phase deletion because the
  // column being dropped will not exist whether the transaction is a success or not, and no other
  // clients can see this column.
  if (pb.ysql_ddl_txn_verifier_state_size() == 0) {
    return true;
  }

  // ysql_ddl_txn_verifier_state is a repeated field with as many entries as number of savepoints
  // in the transaction so far. Since we don't support savepoints for DDL transactions today,
  // default to picking the first entry.
  const auto& state = pb.ysql_ddl_txn_verifier_state(0);
  if (state.contains_create_table_op()) {
    return false;
  }
  if (!state.contains_alter_table_op() || !state.has_previous_schema()) {
    return true;
  }

  // Find whether this column was added in this transaction. This should be done by comparing the
  // 'order' field which corresponds to postgres' attnum field. Comparing the name can result in
  // erroneous conditions if the column was renamed in this transaction.
  int order = -1;
  // Find the order of column to be deleted. We can find the column based on the name, as columns
  // are uniquely named.
  for (const auto& col : pb.schema().columns()) {
    if (col.name() == col_name) {
      order = col.order();
      break;
    }
  }

  SCHECK(order != -1, NotFound, "Column for deletion not found", col_name);

  // Find column with same order in previous schema.
  for (const auto& col : state.previous_schema().columns()) {
    if (col.order() == order) {
      // This column already existed before the current transaction. Therefore it is not being
      // added as part of this transaction and needs to be deleted using 2 phase deletion.
      return true;
    }
  }
  // This column was added as part of this transaction and need not be visible to any other
  // transactions. It is safe to drop right away.
  return false;
}

Status ApplyAlterSteps(server::Clock* clock,
                       const TableId& table_id,
                       const SysTablesEntryPB& current_pb,
                       const AlterTableRequestPB* req,
                       Schema* new_schema,
                       ColumnId* next_col_id,
                       std::vector<DdlLogEntry>* ddl_log_entries) {
  const SchemaPB& current_schema_pb = current_pb.schema();
  Schema cur_schema;
  RETURN_NOT_OK(SchemaFromPB(current_schema_pb, &cur_schema));

  SchemaBuilder builder(cur_schema);
  if (current_pb.has_next_column_id()) {
    builder.set_next_column_id(ColumnId(current_pb.next_column_id()));
  }
  if (current_pb.has_colocated() && current_pb.colocated()) {
    if (current_schema_pb.table_properties().is_ysql_catalog_table()) {
      builder.set_cotable_id(VERIFY_RESULT(Uuid::FromHexString(req->table().table_id())));
    }
    // Colocation ID is set in schema and cannot be altered.
  }

  for (const AlterTableRequestPB::Step& step : req->alter_schema_steps()) {
    auto time = clock->Now();
    switch (step.type()) {
      case AlterTableRequestPB::ADD_COLUMN: {
        if (!step.has_add_column()) {
          return STATUS(InvalidArgument, "ADD_COLUMN missing column info");
        }

        // Verify that encoding is appropriate for the new column's type.
        ColumnSchemaPB new_col_pb = step.add_column().schema();
        if (new_col_pb.has_id()) {
          return STATUS_SUBSTITUTE(InvalidArgument,
              "column $0: client should not specify column id", new_col_pb.ShortDebugString());
        }
        ColumnSchema new_col = ColumnSchemaFromPB(new_col_pb);

        RETURN_NOT_OK(builder.AddColumn(new_col));
        ddl_log_entries->emplace_back(time, table_id, current_pb, Format("Add column $0", new_col));
        break;
      }

      case AlterTableRequestPB::DROP_COLUMN: {
        if (!step.has_drop_column()) {
          return STATUS(InvalidArgument, "DROP_COLUMN missing column info");
        }

        const string& col_name = step.drop_column().name();
        if (cur_schema.is_key_column(col_name)) {
          return STATUS(InvalidArgument, "cannot remove a key column");
        }

        if (req->ysql_yb_ddl_rollback_enabled() &&
            VERIFY_RESULT(NeedTwoPhaseDeleteForColumn(current_pb, col_name))) {
          RETURN_NOT_OK(builder.MarkColumnForDeletion(col_name));
          ddl_log_entries->emplace_back(
              time, table_id, current_pb,
              Format("Mark column $0 for deletion", col_name));
        } else {
          RETURN_NOT_OK(builder.RemoveColumn(col_name));
          ddl_log_entries->emplace_back(
              time, table_id, current_pb, Format("Drop column $0", col_name));
        }
        break;
      }

      case AlterTableRequestPB::RENAME_COLUMN: {
        if (!step.has_rename_column()) {
          return STATUS(InvalidArgument, "RENAME_COLUMN missing column info");
        }

        RETURN_NOT_OK(builder.RenameColumn(
            step.rename_column().old_name(),
            step.rename_column().new_name()));
        ddl_log_entries->emplace_back(
            time, table_id, current_pb,
            Format("Rename column $0 => $1", step.rename_column().old_name(),
                   step.rename_column().new_name()));
        break;
      }

      case AlterTableRequestPB::SET_COLUMN_PG_TYPE: {
        if (!step.has_set_column_pg_type()) {
          return STATUS(InvalidArgument, "SET_COLUMN_PG_TYPE missing column info");
        }

        RETURN_NOT_OK(builder.SetColumnPGType(
            step.set_column_pg_type().name(), step.set_column_pg_type().pg_type_oid()));
        ddl_log_entries->emplace_back(
            time, table_id, current_pb,
            Format(
                "Set pg_type_oid for column $0 => $1", step.set_column_pg_type().name(),
                step.set_column_pg_type().pg_type_oid()));
        break;
      }

        // TODO: EDIT_COLUMN.

      default: {
        return STATUS_SUBSTITUTE(InvalidArgument, "Invalid alter step type: $0", step.type());
      }
    }
  }

  if (req->has_alter_properties()) {
    RETURN_NOT_OK(builder.AlterProperties(req->alter_properties()));
  }

  *new_schema = builder.Build();
  *next_col_id = builder.next_column_id();
  return Status::OK();
}

// Verifies that the request steps are to drop columns, the columns are not present in the YB
// master's schema, and the columns being dropped were dropped previously in the prior version. The
// PG restore process created the table with a dummy column in this column's place, but we returned
// early from CreateTable, whose existing schema doesn't have the dropped column. For use only
// during a ysql major catalog upgrade.
Status VerifyDroppedColumnsForUpgrade(
    const Schema& schema,
    const google::protobuf::RepeatedPtrField<AlterTableRequestPB::Step>& steps) {
  for (const auto& step : steps) {
    const string& col_name = step.drop_column().name();
    if (step.type() != AlterTableRequestPB::DROP_COLUMN) {
      return STATUS_FORMAT(
          InvalidArgument, "Invalid alter table type $0 during ysql major catalog upgrade",
          AlterTableRequestPB::StepType_Name(step.type()));
    }

    if (schema.find_column(col_name) != Schema::kColumnNotFound) {
      return STATUS(IllegalState, "Column unexpectedly found while doing drop column during ysql "
                    "major version upgrade");
    }
    // Name specified by heap.c:RemoveAttributeById(), "........pg.dropped.#........"
    const std::string kDroppedPrefix = "........pg.dropped.";
    const std::string kDroppedSuffix = "........";
    if (!(col_name.starts_with(kDroppedPrefix) && col_name.ends_with(kDroppedSuffix))) {
      return STATUS_FORMAT(InvalidArgument, "Attempting to drop unexpected column '$0' during ysql "
                           "major version upgrade", col_name);
    }
    size_t end_dots = col_name.find(kDroppedSuffix, kDroppedPrefix.length());
    const std::string maybe_number = col_name.substr(kDroppedPrefix.length(),
                                                    end_dots - kDroppedPrefix.length());
    if (!std::all_of(maybe_number.begin(), maybe_number.end(),
                     [](unsigned char c) { return std::isdigit(c); })) {
      return STATUS_FORMAT(InvalidArgument, "Attempting to drop unexpected column '$0' during ysql "
                           "major version upgrade", col_name);
    }
    LOG(INFO) << "Ignoring missing column '" << col_name
              << "' during ALTER TABLE DROP COLUMN in a ysql major catalog upgrade";
  }

  return Status::OK();
}

} // namespace

Status CatalogManager::AlterTable(const AlterTableRequestPB* req,
                                  AlterTableResponsePB* resp,
                                  rpc::RpcContext* rpc,
                                  const LeaderEpoch& epoch) {
  LOG_WITH_PREFIX(INFO) << "Servicing " << __func__ << " request from " << RequestorString(rpc)
                        << ": " << req->ShortDebugString();

  if (IsYsqlMajorCatalogUpgradeInProgress()) {
    // Alter table commands done during the upgrade are catalog changes only.
    LOG(INFO) << "Ignoring alter table request during ysql major catalog upgrade";
    return Status::OK();
  }

  std::vector<DdlLogEntry> ddl_log_entries;

  // Lookup the table and verify if it exists.
  TRACE("Looking up table");
  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTable(req->table()));

  if (table->GetTableType() == PGSQL_TABLE_TYPE) {
    LOG_WITH_PREFIX(INFO) << "PG table OID for AlterTable request: " << table->GetPgTableOid();
  }
  NamespaceId new_namespace_id;

  if (req->has_new_namespace()) {
    // Lookup the new namespace and verify if it exists.
    TRACE("Looking up new namespace");
    scoped_refptr<NamespaceInfo> ns;
    NamespaceIdentifierPB namespace_identifier = req->new_namespace();
    // Use original namespace_id as new_namespace_id for YSQL tables.
    if (table->GetTableType() == PGSQL_TABLE_TYPE && !namespace_identifier.has_id()) {
      namespace_identifier.set_id(table->namespace_id());
    }
    ns = VERIFY_NAMESPACE_FOUND(FindNamespace(namespace_identifier), resp);

    auto ns_lock = ns->LockForRead();
    new_namespace_id = ns->id();
    // Don't use Namespaces that aren't running.
    if (ns->state() != SysNamespaceEntryPB::RUNNING) {
      Status s = STATUS_SUBSTITUTE(TryAgain,
          "Namespace not running (State=$0). Cannot create $1.$2",
          SysNamespaceEntryPB::State_Name(ns->state()), ns->name(), table->name() );
      return SetupError(resp->mutable_error(), NamespaceMasterError(ns->state()), s);
    }
  }
  if (req->has_new_namespace() || req->has_new_table_name()) {
    if (new_namespace_id.empty()) {
      const Status s = STATUS(InvalidArgument, "No namespace used");
      return SetupError(resp->mutable_error(), MasterErrorPB::NO_NAMESPACE_USED, s);
    }
  }

  if (!FLAGS_ysql_yb_enable_replica_identity &&
      req->alter_properties().has_ysql_replica_identity()) {
    const Status s = STATUS(
        InvalidArgument,
        "Cannot use Replica Identity with Alter Table as the flag ysql_yb_enable_replica_identity "
        "is not set");
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
  }

  if (req->ysql_yb_ddl_rollback_enabled()) {
    DCHECK(req->has_transaction());
    DCHECK(req->transaction().has_transaction_id());
  }

  TRACE("Locking table");
  auto l = table->LockForWrite();
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(l, resp));

  if (l->pb.state() == SysTablesEntryPB::PREPARING) {
    return STATUS_EC_FORMAT(
        TryAgain, MasterError(MasterErrorPB::TABLE_NOT_RUNNING), "Table $0 is not ready yet",
        table->name());
  }

  // If the table is already undergoing an alter operation, return failure.
  if (req->ysql_yb_ddl_rollback_enabled() && l->has_ysql_ddl_txn_verifier_state()) {
    if (l->pb_transaction_id() != req->transaction().transaction_id()) {
      auto txn_meta = VERIFY_RESULT(TransactionMetadata::FromPB(l->pb.transaction()));
      auto req_txn_meta = VERIFY_RESULT(TransactionMetadata::FromPB(req->transaction()));
      RETURN_NOT_OK(TriggerDdlVerificationIfNeeded(txn_meta, epoch));
      return STATUS_EC_FORMAT(TryAgain,
            MasterError(MasterErrorPB::TABLE_SCHEMA_CHANGE_IN_PROGRESS),
            "DDL transaction $0 cannot continue as table $1 is undergoing DDL transaction "
            "verification for $2", req_txn_meta.transaction_id, table->name(),
            txn_meta.transaction_id);
    }
  }

  bool has_changes = false;
  auto& table_pb = l.mutable_data()->pb;
  const TableName table_name = l->name();
  const NamespaceId namespace_id = l->namespace_id();
  const TableName new_table_name = req->has_new_table_name() ? req->new_table_name() : table_name;

  while (FLAGS_TEST_block_alter_table == "alter_schema") {
    constexpr auto kSleepFor = 100ms;
    LOG(INFO) << Format("Blocking $0 for $1ms", __func__, kSleepFor);
    SleepFor(kSleepFor);
  }

  // Calculate new schema for the on-disk state, not persisted yet.
  Schema new_schema;
  Schema previous_schema;
  RETURN_NOT_OK(SchemaFromPB(l->pb.schema(), &previous_schema));
  string previous_table_name = l->pb.name();
  ColumnId next_col_id = ColumnId(l->pb.next_column_id());
  if (req->alter_schema_steps_size() || req->has_alter_properties() || req->has_pgschema_name()) {
    if (IsYsqlMajorCatalogUpgradeInProgress()) {
      // In a ysql major catalog upgrade, to ensure the new version's PG catalog is semantically
      // identical to the old version's catalog, pg_restore goes through the motions of creating a
      // table with the dropped columns and then dropping them. However, the catalog manager
      // maintains its own schema state that's invariant across ysql major versions, and from the
      // original drop column request, it has already deleted dropped columns from its own
      // representation of the schema. Since we don't need to make any further changes to the
      // catalog manager's schema during the pg_restore process, in ysql major catalog upgrade mode,
      // we simply verify the alter table command and catalog manager state are as expected, and
      // return early.
      return VerifyDroppedColumnsForUpgrade(previous_schema, req->alter_schema_steps());
    } else {
      TRACE("Apply alter schema");
      Status s = ApplyAlterSteps(
          master_->clock(), table->id(), l->pb, req, &new_schema, &next_col_id, &ddl_log_entries);
      if (!s.ok()) {
        return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
      }
      DCHECK_NE(next_col_id, 0);
      DCHECK_EQ(new_schema.find_column_by_id(next_col_id),
                static_cast<int>(Schema::kColumnNotFound));
      has_changes = true;
    }
  }

  if (req->has_pgschema_name()) {
    new_schema.SetSchemaName(req->pgschema_name());
    // TODO (Oleg): It can be optimized: we don't need to call SendAlterTableRequest()
    //              if only the 'pgschema_name' is changed in the schema, because it's only
    //              the master-level change - no need to notify TServers in this case.
    //              Exception: TabletStatusPB::pgschema_name.
    has_changes = true;
  }

  // Try to acquire the new table name.
  if (req->has_new_namespace() || req->has_new_table_name()) {

    // Postgres handles name uniqueness constraints in it's own layer.
    if (l->table_type() != PGSQL_TABLE_TYPE) {
      LockGuard lock(mutex_);
      VLOG_WITH_FUNC(3) << "Acquired the catalog manager lock";

      TRACE("Acquired catalog manager lock");

      // Verify that the table does not exist.
      scoped_refptr<TableInfo> other_table = FindPtrOrNull(
          table_names_map_, {new_namespace_id, new_table_name});
      if (other_table != nullptr) {
        Status s = STATUS_SUBSTITUTE(AlreadyPresent,
            "Object '$0.$1' already exists",
            GetNamespaceNameUnlocked(new_namespace_id), other_table->name());
        LOG(WARNING) << "Found table: " << other_table->ToStringWithState()
                     << ". Failed alterring table with error: "
                     << s.ToString() << " Request:\n" << req->DebugString();
        return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT, s);
      }

      // Acquire the new table name (now we have 2 name for the same table).
      table_names_map_[{new_namespace_id, new_table_name}] = table;
    }

    table_pb.set_namespace_id(new_namespace_id);
    table_pb.set_name(new_table_name);

    has_changes = true;
  }

  // Check if there has been any changes to the placement policies for this table.
  if (req->has_replication_info()) {
    if (table->GetTableType() == PGSQL_TABLE_TYPE) {
      const Status s = STATUS(InvalidArgument,
            "Placement policy cannot be altered for YSQL tables, use Tablespaces");
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }
    // Validate table replication info.
    RETURN_NOT_OK(ValidateTableReplicationInfo(req->replication_info()));
    table_pb.mutable_replication_info()->CopyFrom(req->replication_info());
    has_changes = true;
  }

  if (req->increment_schema_version()) {
    has_changes = true;
  }

  // TODO(hector): Simplify the AlterSchema workflow to avoid doing the same checks on every layer
  // this request goes through: https://github.com/YugaByte/yugabyte-db/issues/1882.
  if (req->has_wal_retention_secs()) {
    if (has_changes) {
      const Status s = STATUS(InvalidArgument,
          "wal_retention_secs cannot be altered concurrently with other properties");
      return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
    }
    table_pb.set_wal_retention_secs(req->wal_retention_secs());
    has_changes = true;
  }

  if (!has_changes) {
    if (req->has_force_send_alter_request() && req->force_send_alter_request()) {
      RETURN_NOT_OK(SendAlterTableRequest(table, epoch, req));
    }
    // Skip empty requests...
    return Status::OK();
  }

  // Serialize the schema Increment the version number.
  if (new_schema.initialized()) {
    if (!l->pb.has_fully_applied_schema()) {
      // The idea here is that if we are in the middle of updating the schema
      // from one state to another, then YBClients will be given the older
      // version until the schema is updated on all the tablets.
      // As of Dec 2019, this may lead to some rejected operations/retries during
      // the index backfill. See #3284 for possible optimizations.
      MultiStageAlterTable::CopySchemaDetailsToFullyApplied(&table_pb);
    }
    SchemaToPB(new_schema, table_pb.mutable_schema());
  }

  // Only increment the version number if it is a schema change (AddTable change goes through a
  // different path and it's not processed here).
  if (!req->has_wal_retention_secs()) {
    table_pb.set_version(table_pb.version() + 1);
    table_pb.set_updates_only_index_permissions(false);
  }
  table_pb.set_next_column_id(next_col_id);
  l.mutable_data()->set_state(
      SysTablesEntryPB::ALTERING,
      Substitute("Alter table version=$0 ts=$1", table_pb.version(), LocalTimeAsString()));

  RETURN_NOT_OK(UpdateSysCatalogWithNewSchema(
      table, ddl_log_entries, new_namespace_id, new_table_name, epoch, resp));

  // Remove the old name. Not present if PGSQL.
  if (table->GetTableType() != PGSQL_TABLE_TYPE &&
      (req->has_new_namespace() || req->has_new_table_name())) {
    TRACE("Removing (namespace, table) combination ($0, $1) from by-name map",
          namespace_id, table_name);
    LockGuard lock(mutex_);
    table_names_map_.erase({namespace_id, table_name});
  }

  // Update a task to rollback alter if the corresponding YSQL transaction
  // rolls back.
  TransactionMetadata txn;
  TransactionId txn_id = TransactionId::Nil();
  bool schedule_ysql_txn_verifier = false;
  bool need_remove_ddl_state = false;
  // DDL rollback is not applicable for the alter change that sets wal_retention_secs.
  if (!req->has_wal_retention_secs()) {
    if (!req->ysql_yb_ddl_rollback_enabled()) {
      // If DDL rollback is no longer enabled, make sure that there is no transaction
      // verification state present.
      if (l->has_ysql_ddl_txn_verifier_state()) {
        txn_id = VERIFY_RESULT(TransactionMetadata::FromPB(l->pb.transaction())).transaction_id;
        LOG(INFO) << "Clearing ysql_ddl_txn_verifier state for table " << table->ToString();
        table_pb.clear_ysql_ddl_txn_verifier_state();
        need_remove_ddl_state = true;
      }
    } else if (req->has_transaction() && table->GetTableType() == PGSQL_TABLE_TYPE) {
      if (!l->has_ysql_ddl_txn_verifier_state()) {
        table_pb.mutable_transaction()->CopyFrom(req->transaction());
        auto *ddl_state = table_pb.add_ysql_ddl_txn_verifier_state();
        SchemaToPB(previous_schema, ddl_state->mutable_previous_schema());
        ddl_state->set_previous_table_name(previous_table_name);
        schedule_ysql_txn_verifier = true;
      }
      txn = VERIFY_RESULT(TransactionMetadata::FromPB(req->transaction()));
      RSTATUS_DCHECK(!txn.status_tablet.empty(), Corruption, "Given incomplete Transaction");
      DCHECK_EQ(table_pb.ysql_ddl_txn_verifier_state_size(), 1);
      table_pb.mutable_ysql_ddl_txn_verifier_state(0)->set_contains_alter_table_op(true);
    }
  }
  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l.Commit();

  if (need_remove_ddl_state) {
    RemoveDdlTransactionState(table->id(), {txn_id});
  }

  // Verify Transaction gets committed, which occurs after table alter finishes.
  if (schedule_ysql_txn_verifier) {
    RETURN_NOT_OK(ScheduleYsqlTxnVerification(table, txn, epoch));
  }
  RETURN_NOT_OK(SendAlterTableRequest(table, epoch, req));

  // Increment transaction status version if needed.
  if (table->GetTableType() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    RETURN_NOT_OK(IncrementTransactionTablesVersion());
  }

  LOG(INFO) << "Successfully initiated ALTER TABLE (pending tablet schema updates) for "
            << table->ToString() << " per request from " << RequestorString(rpc);

  while (FLAGS_TEST_block_alter_table == "completion") {
    constexpr auto kSleepFor = 100ms;
    LOG(INFO) << Format("Blocking $0 for $1ms", __func__, kSleepFor);
    SleepFor(kSleepFor);
  }

  return Status::OK();
}

Status CatalogManager::UpdateSysCatalogWithNewSchema(
    const scoped_refptr<TableInfo>& table,
    const std::vector<DdlLogEntry>& ddl_log_entries,
    const string& new_namespace_id,
    const string& new_table_name,
    const LeaderEpoch& epoch,
    AlterTableResponsePB* resp) {
  TRACE("Updating metadata on disk");
  std::vector<const DdlLogEntry*> ddl_log_entry_pointers;
  ddl_log_entry_pointers.reserve(ddl_log_entries.size());
  for (const auto& entry : ddl_log_entries) {
    ddl_log_entry_pointers.push_back(&entry);
  }
  Status s = sys_catalog_->Upsert(epoch, ddl_log_entry_pointers, table);
  if (!s.ok()) {
    s = s.CloneAndPrepend(
        Substitute("An error occurred while updating sys-catalog tables entry: $0",
                   s.ToString()));
    LOG(WARNING) << s.ToString();
    if (table->GetTableType() != PGSQL_TABLE_TYPE &&
        (!new_namespace_id.empty() || !new_table_name.empty())) {
      LockGuard lock(mutex_);
      VLOG_WITH_FUNC(3) << "Acquired the catalog manager lock";
      CHECK_EQ(table_names_map_.erase({new_namespace_id, new_table_name}), 1);
    }
    if (resp)
      return CheckIfNoLongerLeaderAndSetupError(s, resp);

    return CheckIfNoLongerLeader(s);
  }
  return Status::OK();
}

Status CatalogManager::IsAlterTableDone(const IsAlterTableDoneRequestPB* req,
                                        IsAlterTableDoneResponsePB* resp) {
  // 1. Lookup the table and verify if it exists.
  TRACE("Looking up table");
  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTable(req->table()));

  TRACE("Locking table");
  auto l = table->LockForRead();
  RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(l, resp));

  // 2. Verify if the alter is in-progress.
  TRACE("Verify if there is an alter operation in progress for $0", table->ToString());
  resp->set_schema_version(l->pb.version());
  resp->set_done(l->pb.state() != SysTablesEntryPB::ALTERING);

  return Status::OK();
}

Status CatalogManager::RegisterNewTabletsForSplit(
    TabletInfo* source_tablet_info, const std::vector<TabletInfoPtr>& new_tablets,
    const LeaderEpoch& epoch, TableInfo::WriteLock* table_write_lock,
    TabletInfo::WriteLock* parent_write_lock) {
  const auto tablet_lock = source_tablet_info->LockForRead();

  auto table = source_tablet_info->table();
  const auto& source_tablet_meta = tablet_lock->pb;
  const auto new_split_depth = source_tablet_meta.split_depth() + 1;
  for (auto& new_tablet : new_tablets) {
    auto& new_tablet_meta = new_tablet->mutable_metadata()->mutable_dirty()->pb;
    new_tablet_meta.mutable_committed_consensus_state()->CopyFrom(
        source_tablet_meta.committed_consensus_state());
    new_tablet_meta.set_split_depth(new_split_depth);
    new_tablet_meta.set_split_parent_tablet_id(source_tablet_info->tablet_id());

    parent_write_lock->mutable_data()->pb.add_split_tablet_ids(new_tablet->id());
  }

  int new_partition_list_version;
  auto& table_pb = table_write_lock->mutable_data()->pb;
  new_partition_list_version = table_pb.partition_list_version() + 1;
  table_pb.set_partition_list_version(new_partition_list_version);

  RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table, new_tablets, source_tablet_info));

  MAYBE_FAULT(FLAGS_TEST_crash_after_registering_split_tablets);
  if (PREDICT_FALSE(FLAGS_TEST_error_after_registering_split_tablets)) {
    return STATUS(IllegalState, "TEST: error happened while registering a new tablet.");
  }

  // This has to be done while the table lock is held since TableInfo::partitions_ must be updated
  // at the same time as the partition list version.
  RETURN_NOT_OK(table->AddTablets(new_tablets));
  // TODO: We use this pattern in other places, but what if concurrent thread accesses not yet
  // committed TabletInfo from the `table` ?
  for (auto& new_tablet : new_tablets) {
    const PartitionPB& partition = new_tablet->metadata().state().pb.partition();
    LOG(INFO) << "Registered new tablet " << new_tablet->tablet_id() << " (partition_key_start: "
              << Slice(partition.partition_key_start()).ToDebugString(/* max_length = */ 64)
              << ", partition_key_end: "
              << Slice(partition.partition_key_end()).ToDebugString(/* max_length = */ 64)
              << ", split_depth: " << new_split_depth << ") to split the tablet "
              << source_tablet_info->tablet_id() << " (" << AsString(source_tablet_meta.partition())
              << ") for table " << table->ToString()
              << ", new partition_list_version: " << new_partition_list_version;
    new_tablet->mutable_metadata()->CommitMutation();
  }
  return Status::OK();
}

Result<NamespaceId> CatalogManager::GetTableNamespaceId(TableId table_id) {
  SharedLock lock(mutex_);
  auto table = tables_->FindTableOrNull(table_id);
  if (!table || table->is_deleted()) {
    return STATUS(NotFound, Format("Table $0 is not found", table_id));
  }
  return table->namespace_id();
}

Status CatalogManager::GetTableSchema(const GetTableSchemaRequestPB* req,
                                      GetTableSchemaResponsePB* resp) {
  return GetTableSchemaInternal(req, resp, /* always_get_fully_applied_indexes= */ false);
}

Status CatalogManager::GetTableSchemaInternal(const GetTableSchemaRequestPB* req,
                                              GetTableSchemaResponsePB* resp,
                                              bool always_get_fully_applied_indexes) {
  VLOG(1) << "Servicing GetTableSchema request for " << req->ShortDebugString();

  // Lookup the table and verify if it exists.
  TRACE("Looking up table");
  auto table = VERIFY_RESULT(FindTable(req->table(), !req->check_exists_only()));
  if (req->check_exists_only()) {
    return Status::OK();
  }

  // Due to differences in the way proxies handle version mismatch (pull for yql vs push for sql).
  // For YQL tables, we will return the "set of indexes" being applied instead of the ones
  // that are fully completed.
  // For PGSQL (and other) tables we want to return the fully applied schema.
  auto get_fully_applied_indexes =
      always_get_fully_applied_indexes || table->GetTableType() != TableType::YQL_TABLE_TYPE;

  TRACE("Locking table");
  auto l = table->LockForRead();
  if (req->include_hidden()) {
    // Do not return the schema of a deleted table even if include_hidden is set to true
    SCHECK_EC_FORMAT(
        l->is_running(), NotFound, MasterError(MasterErrorPB::OBJECT_NOT_FOUND),
        "The object '$0.$1' is not running", l->namespace_id(), l->name());
  } else {
    RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(l, resp));
  }
  if (get_fully_applied_indexes && l->pb.has_fully_applied_schema()) {
    // An AlterTable is in progress; fully_applied_schema is the last
    // schema that has reached every TS.
    DCHECK(l->pb.state() == SysTablesEntryPB::ALTERING);
    resp->mutable_schema()->CopyFrom(l->pb.fully_applied_schema());
  } else {
    // Case 1: There's no AlterTable, the regular schema is "fully applied".
    // Case 2: get_fully_applied_indexes == false (for YCQL). Always return the latest schema.
    resp->mutable_schema()->CopyFrom(l->pb.schema());
  }

  // Due to pgschema_name being added after 2.13, older YSQL tables may not have this field.
  // So backfill pgschema_name for older YSQL tables. Skip for some special cases.
  if (l->table_type() == TableType::PGSQL_TABLE_TYPE && resp->schema().pgschema_name().empty() &&
      !table->is_system() && !table->IsSequencesSystemTable() &&
      !table->IsColocationParentTable()) {
    SharedLock lock(mutex_);
    TRACE("Acquired catalog manager lock for schema name lookup");

    auto pgschema_name = GetPgSchemaName(table->id(), l.data());
    if (!pgschema_name.ok() || pgschema_name->empty()) {
      LOG(WARNING) << Format(
          "Unable to find schema name for YSQL table $0.$1 due to error: $2",
          table->namespace_name(), table->name(), pgschema_name.ToString());
    } else {
      resp->mutable_schema()->set_pgschema_name(*pgschema_name);
    }
  }

  if (get_fully_applied_indexes && l->pb.has_fully_applied_schema()) {
    resp->set_version(l->pb.fully_applied_schema_version());
    resp->mutable_indexes()->CopyFrom(l->pb.fully_applied_indexes());
    if (l->pb.has_fully_applied_index_info()) {
      resp->set_obsolete_indexed_table_id(GetIndexedTableId(l->pb));
      *resp->mutable_index_info() = l->pb.fully_applied_index_info();
    }
    VLOG(1) << "Returning"
            << "\nfully_applied_schema with version "
            << l->pb.fully_applied_schema_version()
            << ":\n"
            << yb::ToString(l->pb.fully_applied_indexes())
            << "\ninstead of schema with version "
            << l->pb.version()
            << ":\n"
            << yb::ToString(l->pb.indexes());
  } else {
    resp->set_version(l->pb.version());
    resp->mutable_indexes()->CopyFrom(l->pb.indexes());
    if (l->pb.has_index_info()) {
      resp->set_obsolete_indexed_table_id(GetIndexedTableId(l->pb));
      *resp->mutable_index_info() = l->pb.index_info();
    }
    VLOG(3) << "Returning"
            << "\nschema with version "
            << l->pb.version()
            << ":\n"
            << yb::ToString(l->pb.indexes());
  }

  resp->set_is_backfilling(table->IsBackfilling());

  resp->set_is_compatible_with_previous_version(l->pb.updates_only_index_permissions());
  resp->mutable_partition_schema()->CopyFrom(l->pb.partition_schema());
  if (IsReplicationInfoSet(l->pb.replication_info())) {
    resp->mutable_replication_info()->CopyFrom(l->pb.replication_info());
  }
  resp->set_create_table_done(!table->IsCreateInProgress());
  resp->set_table_type(table->metadata().state().pb.table_type());
  resp->mutable_identifier()->set_table_name(l->pb.name());
  resp->mutable_identifier()->set_table_id(table->id());
  resp->mutable_identifier()->mutable_namespace_()->set_id(table->namespace_id());
  auto nsinfo = FindNamespaceById(table->namespace_id());
  if (nsinfo.ok()) {
    resp->mutable_identifier()->mutable_namespace_()->set_name((**nsinfo).name());
  }

  if (l->pb.has_wal_retention_secs()) {
    resp->set_wal_retention_secs(l->pb.wal_retention_secs());
  }

  // Get namespace name by id.
  SharedLock lock(mutex_);
  TRACE("Looking up namespace");
  const scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, table->namespace_id());

  if (ns == nullptr) {
    Status s = STATUS_SUBSTITUTE(
        NotFound, "Could not find namespace by namespace id $0 for request $1.",
        table->namespace_id(), req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
  }

  resp->mutable_identifier()->mutable_namespace_()->set_name(ns->name());

  resp->set_colocated(table->colocated());

  if (table->IsColocatedUserTable()) {
    // Set the tablegroup_id for colocated user tables only after Colocation is GA.
    if (IsTablegroupParentTableId(table->LockForRead()->pb.parent_table_id())) {
      resp->set_tablegroup_id(
          GetTablegroupIdFromParentTableId(table->LockForRead()->pb.parent_table_id()));
    }
  }

  if (!table->pg_table_id().empty()) {
    resp->set_pg_table_id(table->pg_table_id());
  }

  if (l->has_ysql_ddl_txn_verifier_state()) {
    resp->add_ysql_ddl_txn_verifier_state()->CopyFrom(l->ysql_ddl_txn_verifier_state());
  }

  VLOG(1) << "Serviced GetTableSchema request for " << req->ShortDebugString() << " with "
          << yb::ToString(*resp);
  return Status::OK();
}

Status CatalogManager::GetTablegroupSchema(const GetTablegroupSchemaRequestPB* req,
                                           GetTablegroupSchemaResponsePB* resp) {
  VLOG(1) << "Servicing GetTablegroupSchema request for " << req->ShortDebugString();
  if (!req->tablegroup().has_id()) {
    Status s = STATUS(InvalidArgument, "Invalid GetTablegroupSchema request (missing ID)");
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
  }

  const std::string& tablegroup_id = req->tablegroup().id();
  if (!IsIdLikeUuid(tablegroup_id)) {
    Status s = STATUS_FORMAT(InvalidArgument, "Malformed tablegroup ID: $0", tablegroup_id);
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
  }

  // Lookup the tablegroup.
  std::unordered_set<TableId> table_ids;
  {
    SharedLock lock(mutex_);

    const auto* tablegroup = tablegroup_manager_->Find(tablegroup_id);
    SCHECK(tablegroup, NotFound, Substitute("Tablegroup with ID $0 not found", tablegroup_id));
    table_ids = tablegroup->ChildTableIds();
  }

  for (const auto& table_id : table_ids) {
    TRACE("Looking up table");
    GetTableSchemaRequestPB schema_req;
    GetTableSchemaResponsePB schema_resp;
    schema_req.mutable_table()->set_table_id(table_id);
    Status s = GetTableSchema(&schema_req, &schema_resp);
    if (!s.ok() || schema_resp.has_error()) {
      LOG(ERROR) << "Error while getting table schema: " << s;
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, s);
    }
    resp->add_get_table_schema_response_pbs()->Swap(&schema_resp);
  }

  return Status::OK();
}

Status CatalogManager::GetColocatedTabletSchema(const GetColocatedTabletSchemaRequestPB* req,
                                                GetColocatedTabletSchemaResponsePB* resp) {
  VLOG(1) << "Servicing GetColocatedTabletSchema request for " << req->ShortDebugString();

  // Lookup the given parent colocated table and verify if it exists.
  TRACE("Looking up table");
  auto parent_colocated_table = VERIFY_RESULT(FindTable(req->parent_colocated_table()));
  {
    TRACE("Locking table");
    auto l = parent_colocated_table->LockForRead();
    RETURN_NOT_OK(CatalogManagerUtil::CheckIfTableDeletedOrNotVisibleToClient(l, resp));
  }

  if (!parent_colocated_table->colocated() || !parent_colocated_table->IsColocatedDbParentTable()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_TABLE_TYPE,
                      STATUS(InvalidArgument, "Table provided is not a parent colocated table"));
  }

  // Next get all the user tables that are in the database.
  ListTablesRequestPB listTablesReq;
  ListTablesResponsePB ListTablesResp;

  listTablesReq.mutable_namespace_()->set_id(parent_colocated_table->namespace_id());
  listTablesReq.mutable_namespace_()->set_database_type(YQL_DATABASE_PGSQL);
  listTablesReq.set_exclude_system_tables(true);
  Status status = ListTables(&listTablesReq, &ListTablesResp);
  if (!status.ok() || ListTablesResp.has_error()) {
    LOG(ERROR) << "Error while listing tables: " << status;
    return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, status);
  }

  // Get the table schema for each colocated table.
  for (const auto& t : ListTablesResp.tables()) {
    // Need to check if this table is colocated first.
    TRACE("Looking up table");
    scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTableById(t.id()));

    if (table->colocated()) {
      // Now we can get the schema for this table.
      GetTableSchemaRequestPB schemaReq;
      GetTableSchemaResponsePB schemaResp;
      schemaReq.mutable_table()->set_table_id(t.id());
      status = GetTableSchema(&schemaReq, &schemaResp);
      if (!status.ok() || schemaResp.has_error()) {
        LOG(ERROR) << "Error while getting table schema: " << status;
        return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, status);
      }
      resp->add_get_table_schema_response_pbs()->Swap(&schemaResp);
    }
  }

  return Status::OK();
}

Status CatalogManager::ListTables(const ListTablesRequestPB* req,
                                  ListTablesResponsePB* resp) {
  NamespaceIdentifierPB req_namespace;
  NamespaceIdentifierPB* namespace_info = nullptr;

  // Validate namespace.
  if (req->has_namespace_()) {
    // Lookup the namespace and verify if it exists.
    auto ns = VERIFY_NAMESPACE_FOUND(FindNamespace(req->namespace_()), resp);

    auto ns_lock = ns->LockForRead();

    // Don't list tables with a namespace that isn't running.
    if (ns->state() != SysNamespaceEntryPB::RUNNING) {
      LOG(INFO) << "ListTables request for a Namespace not running (State="
                << SysNamespaceEntryPB::State_Name(ns->state()) << ")";
      return Status::OK();
    }

    req_namespace.set_id(ns->id());
    req_namespace.set_name(ns_lock->name());
    req_namespace.set_database_type(ns_lock->pb.database_type());
    namespace_info = &req_namespace;
  }

  bool has_rel_filter = req->relation_type_filter_size() > 0;
  std::array<bool, RelationType_ARRAYSIZE> include_type;
  include_type.fill(!has_rel_filter);
  include_type[SYSTEM_TABLE_RELATION] =
      include_type[SYSTEM_TABLE_RELATION] && !req->exclude_system_tables();

  for (auto relation : req->relation_type_filter()) {
    include_type[relation] = true;
  }

  SharedLock lock(mutex_);
  for (const auto& table_info : tables_->GetAllTables()) {
    auto ltm = table_info->LockForRead();

    if (!ltm->visible_to_client() && !req->include_not_running()) {
      continue;
    }

    const auto& table_namespace_id = ltm->namespace_id();
    if (table_namespace_id.empty() ||
        (!req_namespace.id().empty() && req_namespace.id() != table_namespace_id)) {
      continue; // Skip tables from other namespaces.
    }

    if (req->has_name_filter()) {
      size_t found = ltm->name().find(req->name_filter());
      if (found == string::npos) {
        continue;
      }
    }

    RelationType relation_type;
    if (table_info->IsUserIndex(ltm)) {
      relation_type = INDEX_TABLE_RELATION;
    } else if (ltm->pb.is_matview()) {
      relation_type = MATVIEW_TABLE_RELATION;
    } else if (table_info->IsUserTable(ltm)) {
      relation_type = USER_TABLE_RELATION;
    } else if (table_info->IsColocationParentTable()) {
      if (!include_type[SYSTEM_TABLE_RELATION]) {
        continue;
      }
      relation_type = COLOCATED_PARENT_TABLE_RELATION;
    } else {
      relation_type = SYSTEM_TABLE_RELATION;
    }
    if (!include_type[relation_type]) {
      continue;
    }

    ListTablesResponsePB::TableInfo* table;

    if (!namespace_info || namespace_info->id() != table_namespace_id) {
      auto ns = FindNamespaceByIdUnlocked(table_namespace_id);
      if (!ns.ok()) {
        if (PREDICT_FALSE(FLAGS_TEST_return_error_if_namespace_not_found)) {
          VERIFY_NAMESPACE_FOUND(std::move(ns), resp);
        }
        LOG(WARNING) << "Unable to find namespace with id " << table_namespace_id
                     << " for table " << table_info->ToStringWithState();
        continue;
      }
      auto namespace_lock = (**ns).LockForRead();
      if (namespace_lock->pb.state() != SysNamespaceEntryPB::RUNNING) {
        LOG(WARNING) << "Namespace with id " << table_namespace_id << " for table "
                     << table_info->ToStringWithState() << " is in wrong state: "
                     << SysNamespaceEntryPB::State_Name(namespace_lock->pb.state());
        continue;
      }
      table = resp->add_tables();
      namespace_info = table->mutable_namespace_();
      namespace_info->set_id((**ns).id());
      namespace_info->set_name(namespace_lock->name());
      namespace_info->set_database_type(namespace_lock->pb.database_type());
    } else {
      table = resp->add_tables();
      *table->mutable_namespace_() = *namespace_info;
    }

    table->set_id(table_info->id());
    table->set_name(ltm->name());
    table->set_table_type(ltm->table_type());
    table->set_relation_type(relation_type);
    if (relation_type == INDEX_TABLE_RELATION) {
      table->set_indexed_table_id(table_info->indexed_table_id());
    }
    table->set_state(ltm->pb.state());
    table->set_pgschema_name(ltm->schema().pgschema_name());
    if (table_info->colocated()) {
      table->mutable_colocated_info()->set_colocated(true);
      if (!table_info->IsColocationParentTable() && ltm->pb.has_parent_table_id()) {
        table->mutable_colocated_info()->set_parent_table_id(ltm->pb.parent_table_id());
      }
    }
    table->set_hidden(ltm->is_hidden());
  }
  return Status::OK();
}

scoped_refptr<TableInfo> CatalogManager::GetTableInfoUnlocked(const TableId& table_id) const {
  return tables_->FindTableOrNull(table_id);
}

scoped_refptr<TableInfo> CatalogManager::GetTableInfo(const TableId& table_id) const {
  SharedLock lock(mutex_);
  return GetTableInfoUnlocked(table_id);
}

std::unordered_map<TableId, TableInfoPtr> CatalogManager::GetTableInfos(
    const std::vector<TableId>& table_ids) {
  SharedLock lock(mutex_);
  std::unordered_map<TableId, TableInfoPtr> id_to_table;
  for (const auto& id : table_ids) {
    id_to_table[id] = GetTableInfoUnlocked(id);
  }
  return id_to_table;
}

scoped_refptr<TableInfo> CatalogManager::GetTableInfoFromNamespaceNameAndTableName(
    YQLDatabase db_type, const NamespaceName& namespace_name, const TableName& table_name,
    const PgSchemaName pg_schema_name) {
  SharedLock lock(mutex_);
  const auto ns = FindPtrOrNull(namespace_names_mapper_[db_type], namespace_name);
  if (!ns) {
    return nullptr;
  }

  if (db_type != YQL_DATABASE_PGSQL) {
    return FindPtrOrNull(table_names_map_, {ns->id(), table_name});
  }

  // YQL_DATABASE_PGSQL
  if (pg_schema_name.empty()) {
    return nullptr;
  }

  for (const auto& table : tables_->GetAllTables()) {
    auto l = table->LockForRead();
    auto& table_pb = l->pb;

    if (!l->started_deleting() && table_pb.namespace_id() == ns->id() &&
        boost::iequals(table_pb.schema().pgschema_name(), pg_schema_name) &&
        boost::iequals(table_pb.name(), table_name)) {
      return table;
    }
  }
  return nullptr;
}

Result<std::vector<scoped_refptr<TableInfo>>> CatalogManager::GetTableInfosForNamespace(
    const NamespaceId& namespace_id) const {
  std::vector<scoped_refptr<TableInfo>> table_infos;
  SharedLock lock(mutex_);
  for (const auto& table : tables_->GetAllTables()) {
    auto l = table->LockForRead();
    auto& table_pb = l->pb;

    if (!l->started_deleting() && table_pb.namespace_id() == namespace_id) {
      table_infos.push_back(table);
    }
  }

  return table_infos;
}

std::vector<TableInfoPtr> CatalogManager::GetTables(
    GetTablesMode mode, PrimaryTablesOnly primary_tables_only) {
  std::vector<TableInfoPtr> result;
  // Note: TableInfoPtr has a namespace_name field which was introduced in version 2.3.0. The data
  // for this field is not backfilled (see GH17713/GH17712 for more details).
  {
    SharedLock lock(mutex_);
    auto tables_it = primary_tables_only ? tables_->GetPrimaryTables() : tables_->GetAllTables();
    result = std::vector(std::begin(tables_it), std::end(tables_it));
  }
  switch (mode) {
    case GetTablesMode::kAll:
      return result;
    case GetTablesMode::kRunning: {
      auto filter = [](const TableInfoPtr& table_info) { return !table_info->is_running(); };
      EraseIf(filter, &result);
      return result;
    }
    case GetTablesMode::kVisibleToClient: {
      auto filter = [](const TableInfoPtr& table_info) {
        return !table_info->LockForRead()->visible_to_client();
      };
      EraseIf(filter, &result);
      return result;
    }
  }
  FATAL_INVALID_ENUM_VALUE(GetTablesMode, mode);
}

void CatalogManager::GetAllNamespaces(std::vector<scoped_refptr<NamespaceInfo>>* namespaces,
                                      bool includeOnlyRunningNamespaces) {
  namespaces->clear();
  SharedLock lock(mutex_);
  for (const NamespaceInfoMap::value_type& e : namespace_ids_map_) {
    if (includeOnlyRunningNamespaces && e.second->state() != SysNamespaceEntryPB::RUNNING) {
      continue;
    }
    namespaces->push_back(e.second);
  }
}

void CatalogManager::GetAllUDTypes(std::vector<scoped_refptr<UDTypeInfo>>* types) {
  types->clear();
  SharedLock lock(mutex_);
  for (const UDTypeInfoMap::value_type& e : udtype_ids_map_) {
    types->push_back(e.second);
  }
}

std::vector<std::shared_ptr<MonitoredTask>> CatalogManager::GetRecentTasks() {
  return tasks_tracker_->GetTasks();
}

std::vector<std::shared_ptr<MonitoredTask>> CatalogManager::GetRecentJobs() {
  return jobs_tracker_->GetTasks();
}

Result<NamespaceId> CatalogManager::GetNamespaceId(
    YQLDatabase db_type, const NamespaceName& namespace_name) {
  SharedLock lock(mutex_);
  const auto ns = FindPtrOrNull(namespace_names_mapper_[db_type], namespace_name);
  SCHECK(ns, NotFound, Format("Namespace $0 of type $1 not found", namespace_name, db_type));
  return ns->id();
}

NamespaceName CatalogManager::GetNamespaceNameUnlocked(const NamespaceId& id) const  {
  const scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, id);
  return ns == nullptr ? NamespaceName() : ns->name();
}

NamespaceName CatalogManager::GetNamespaceName(const NamespaceId& id) const {
  TRACE("Acquired catalog manager lock");
  SharedLock lock(mutex_);
  return GetNamespaceNameUnlocked(id);
}

NamespaceName CatalogManager::GetNamespaceNameUnlocked(
    const scoped_refptr<TableInfo>& table) const  {
  return GetNamespaceNameUnlocked(table->namespace_id());
}

NamespaceName CatalogManager::GetNamespaceName(const scoped_refptr<TableInfo>& table) const {
  return GetNamespaceName(table->namespace_id());
}

void CatalogManager::NotifyPrepareDeleteTransactionTabletFinished(
    const TabletInfoPtr& tablet, const std::string& msg, HideOnly hide_only,
    const LeaderEpoch& epoch) {
  if (!hide_only) {
    auto lock = tablet->LockForWrite();
    lock.mutable_data()->set_state(SysTabletsEntryPB::DELETED, msg);
    WARN_NOT_OK(sys_catalog_->Upsert(epoch, tablet.get()),
                "Failed to set tablet state to DELETED");
    lock.Commit();
  }

  // Transaction tablets have no data, so don't try to delete data.
  DeleteTabletReplicas(tablet, msg, hide_only, KeepData::kTrue, epoch);
}

void CatalogManager::NotifyTabletDeleteFinished(
    const TabletServerId& tserver_uuid, const TabletId& tablet_id, const TableInfoPtr& table,
    const LeaderEpoch& epoch, server::MonitoredTaskState task_state) {
  auto ts_desc_result = master_->ts_manager()->LookupTSByUUID(tserver_uuid);
  if (!ts_desc_result.ok()) {
    LOG(WARNING) << "Unable to find tablet server " << tserver_uuid;
  } else {
    auto num_removed = ts_desc_result.get()->ClearPendingTabletDelete(tablet_id);
    if (num_removed == 0) {
      LOG(WARNING) << "Pending delete for tablet " << tablet_id << " in ts " << tserver_uuid
                   << " doesn't exist";
    }
  }
  if (task_state == server::MonitoredTaskState::kComplete) {
    CheckTableDeleted(table, epoch);
  }
}

Status CatalogManager::CreateTablegroup(
    const CreateTablegroupRequestPB* req, CreateTablegroupResponsePB* resp, rpc::RpcContext* rpc,
    const LeaderEpoch& epoch) {
  CreateTableRequestPB ctreq;
  CreateTableResponsePB ctresp;

  // Sanity check for PB fields.
  if (!req->has_id() || !req->has_namespace_id() || !req->has_namespace_name()) {
    Status s = STATUS(InvalidArgument, "Improper CREATE TABLEGROUP request (missing fields).");
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
  }

  // Use the tablegroup id as the prefix for the parent table id.
  TableId parent_table_id;
  TableName parent_table_name;
  {
    LockGuard lock(mutex_);
    scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, req->namespace_id());
    if (ns == nullptr) {
      Status s = STATUS_SUBSTITUTE(
          NotFound, "Could not find namespace by namespace id $0 for request $1.",
          req->namespace_id(), req->DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
    }
    if (ns->colocated()) {
      parent_table_id = GetColocationParentTableId(req->id());
      parent_table_name = GetColocationParentTableName(req->id());
    } else {
      parent_table_id = GetTablegroupParentTableId(req->id());
      parent_table_name = GetTablegroupParentTableName(req->id());
    }
  }
  if (req->has_transaction()) {
    ctreq.mutable_transaction()->CopyFrom(req->transaction());
    ctreq.set_ysql_yb_ddl_rollback_enabled(req->ysql_yb_ddl_rollback_enabled());
  }
  ctreq.set_name(parent_table_name);
  ctreq.set_table_id(parent_table_id);
  ctreq.mutable_namespace_()->set_name(req->namespace_name());
  ctreq.mutable_namespace_()->set_id(req->namespace_id());
  ctreq.set_table_type(PGSQL_TABLE_TYPE);
  ctreq.set_tablegroup_id(req->id());
  ctreq.set_tablespace_id(req->tablespace_id());

  YBSchemaBuilder schema_builder;
  schema_builder.AddColumn("parent_column")->Type(DataType::BINARY)->PrimaryKey();
  YBSchema ybschema;
  CHECK_OK(schema_builder.Build(&ybschema));
  auto schema = yb::client::internal::GetSchema(ybschema);
  SchemaToPB(schema, ctreq.mutable_schema());
  if (!FLAGS_TEST_tablegroup_master_only) {
    ctreq.mutable_schema()->mutable_table_properties()->set_is_transactional(true);
  }

  // Create a parent table, which will create the tablet and the tablegroup in-memory entity.
  Status s = CreateTable(&ctreq, &ctresp, rpc, epoch);
  resp->set_parent_table_id(ctresp.table_id());
  resp->set_parent_table_name(parent_table_name);

  // Carry over error.
  if (ctresp.has_error()) {
    resp->mutable_error()->Swap(ctresp.mutable_error());
  }

  // We do not lock here so it is technically possible that the table was already created.
  // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
  if (!s.ok() && !s.IsAlreadyPresent()) {
    LOG(WARNING) << "Tablegroup creation failed: " << s.ToString();
  }
  return s;
}

Status CatalogManager::DeleteTablegroup(const DeleteTablegroupRequestPB* req,
                                        DeleteTablegroupResponsePB* resp,
                                        rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing DeleteTablegroup request from " << RequestorString(rpc) << ": "
            << req->ShortDebugString();

  DeleteTableRequestPB dtreq;
  DeleteTableResponsePB dtresp;

  // Sanity check for PB fields
  if (!req->has_id()) {
    Status s = STATUS(InvalidArgument, "Improper DELETE TABLEGROUP request (missing fields).");
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
  }

  // Use the tablegroup id as the prefix for the parent table id.
  TableId parent_table_id;
  TableName parent_table_name;
  {
    SharedLock lock(mutex_);
    const auto* tablegroup = tablegroup_manager_->Find(req->id());

    if (!tablegroup) {
      return SetupError(
          resp->mutable_error(),
          MasterErrorPB::OBJECT_NOT_FOUND,
          STATUS_FORMAT(InvalidArgument, "Tablegroup with ID $0 does not exist", req->id()));
    }

    if (!tablegroup->IsEmpty()) {
      if (!req->has_ysql_yb_ddl_rollback_enabled()) {
        return SetupError(
            resp->mutable_error(),
            MasterErrorPB::INVALID_REQUEST,
            STATUS(InvalidArgument, "Cannot delete tablegroup, it still has tables in it"));
      }

      // Check whether the tables in this tablegroup are marked for deletion.
      const auto tables = tablegroup->ChildTableIds();
      for (const auto& tableid : tables) {
        const auto table = tables_->FindTableOrNull(tableid);
        if (!table) {
          return SetupError(
              resp->mutable_error(),
              MasterErrorPB::OBJECT_NOT_FOUND,
              STATUS_FORMAT(NotFound, "Table with ID $0 in tablegroup $1 does not exist",
                  tableid, req->id()));
        }

        if (!table->IsBeingDroppedDueToDdlTxn(req->transaction().transaction_id(),
                                              true /* txn_success */)) {
          return SetupError(
              resp->mutable_error(),
              MasterErrorPB::INVALID_REQUEST,
              STATUS_FORMAT(
                  InvalidArgument, "Cannot delete non-empty tablegroup, table $0 is not deleted",
                  tableid));
        }
      }
    }

    scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_map_, tablegroup->database_id());
    if (ns == nullptr) {
      Status s = STATUS_SUBSTITUTE(
          NotFound, "Could not find namespace by namespace id $0.",
          tablegroup->database_id());
      return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
    }
    if (ns->colocated()) {
      parent_table_id = GetColocationParentTableId(req->id());
      parent_table_name = GetColocationParentTableName(req->id());
    } else {
      parent_table_id = GetTablegroupParentTableId(req->id());
      parent_table_name = GetTablegroupParentTableName(req->id());
    }
  }

  dtreq.mutable_table()->set_table_name(parent_table_name);
  dtreq.mutable_table()->set_table_id(parent_table_id);
  dtreq.set_is_index_table(false);
  dtreq.mutable_transaction()->CopyFrom(req->transaction());
  dtreq.set_ysql_yb_ddl_rollback_enabled(req->ysql_yb_ddl_rollback_enabled());

  // Delete the parent table.
  // This will also delete the tablegroup tablet, as well as the tablegroup entity.
  Status s = DeleteTable(&dtreq, &dtresp, rpc, epoch);
  resp->set_parent_table_id(dtresp.table_id());

  // Carry over error.
  if (dtresp.has_error()) {
    resp->mutable_error()->Swap(dtresp.mutable_error());
    return s;
  }

  LOG(INFO) << "Deleted tablegroup " << req->id();
  return s;
}

Status CatalogManager::ListTablegroups(const ListTablegroupsRequestPB* req,
                                       ListTablegroupsResponsePB* resp,
                                       rpc::RpcContext* rpc) {
  SharedLock lock(mutex_);

  if (!req->has_namespace_id()) {
    Status s = STATUS(InvalidArgument, "Improper ListTablegroups request (missing fields).");
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
  }

  const auto tablegroups =
      VERIFY_RESULT(tablegroup_manager_->ListForDatabase(req->namespace_id()));

  for (const auto& tablegroup : tablegroups) {
    TablegroupIdentifierPB *tg = resp->add_tablegroups();
    tg->set_id(tablegroup->id());
    tg->set_namespace_id(tablegroup->database_id());
  }
  return Status::OK();
}

Status CatalogManager::CreateNamespace(const CreateNamespaceRequestPB* req,
                                       CreateNamespaceResponsePB* resp,
                                       rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  Status return_status;

  // Copy the request, so we can fill in some defaults.
  LOG(INFO) << "CreateNamespace from " << RequestorString(rpc)
            << ": " << req->DebugString();

  scoped_refptr<NamespaceInfo> ns;
  std::vector<scoped_refptr<TableInfo>> pgsql_tables;
  TransactionMetadata txn;
  const auto db_type = GetDatabaseType(*req);
  NamespaceInfo::WriteLock ns_l;
  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    // Validate the user request.

    const bool is_ysql_major_upgrade_in_progress = IsYsqlMajorCatalogUpgradeInProgress();

    auto check_ns_errors = [req, resp, db_type, is_ysql_major_upgrade_in_progress](
                               const scoped_refptr<NamespaceInfo>& ns, bool by_id) -> Status {
      Status return_status;
      if (is_ysql_major_upgrade_in_progress) {
        // During a ysql major catalog upgrade, each *system* namespace (template1, template0,
        // postgres, yugabyte, and system_platform) is "created" twice: once by initdb, and then
        // once again after a DROP DATABASE that's part of upstream Postgres's dump and restore
        // process. In both cases, the namespace must already exist in DocDB for the prior version,
        // and we reuse it. Therefore, it's an error if ns is nullptr.
        //
        // The first time through this process, ysql_next_major_version_state is NEXT_VER_RUNNING.
        // The second time, it's NEXT_VER_DELETED, as a means to work around the fact we're only
        // effectively "dropping" and "recreating" the not-yet-in-use current-version namespace.
        //
        // User-created namespaces are expected to be "created" once by pg_restore. In this case,
        // again, the DocDB namespace already exists for the prior version, and we will re-use it.
        if (ns == nullptr) {
          std::string context;
          if (by_id) {
            context = "No new namespaces can be created";
          } else {
            context = "Namespace is unexpectedly missing from the namespace names mapper";
          }
          return_status =
              STATUS(IllegalState, StrCat(context, " during a ysql major catalog upgrade"));
          return SetupError(
              resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, return_status);
        }
        RSTATUS_DCHECK_EQ(
            ns->name(), req->name(), InternalError,
            Format(
                "Namespace created during a ysql major catalog upgrade had $0 "
                "that matched a different namespace $1",
                by_id ? "an ID" : "a name", by_id ? "name" : "ID"));
      } else if (ns != nullptr) {
        // If this is the by-id case, and PG OID collision happens, use the PG error code:
        // YB_PG_DUPLICATE_DATABASE to signal PG backend to retry CREATE DATABASE using the next
        // available OID. Otherwise, don't set a customized error code in the return status.
        // This is the default behavior of STATUS().
        resp->set_id(ns->id());
        auto pg_createdb_oid_collision_errcode =
            PgsqlError(YBPgErrorCode::YB_PG_DUPLICATE_DATABASE);
        if (by_id) {
          return_status = STATUS(
              AlreadyPresent, Format("Keyspace with id '$0' already exists", req->namespace_id()),
              Slice(),
              db_type == YQL_DATABASE_PGSQL ? &pg_createdb_oid_collision_errcode : nullptr);
        } else {
          return_status =
              STATUS_SUBSTITUTE(AlreadyPresent, "Keyspace '$0' already exists", req->name());
        }
        LOG(WARNING) << "Found keyspace: " << ns->id()
                     << ". Failed creating keyspace with error: " << return_status.ToString()
                     << " Request:\n"
                     << req->DebugString();
        return SetupError(
            resp->mutable_error(), MasterErrorPB::NAMESPACE_ALREADY_PRESENT, return_status);
      }
      return Status::OK();
    };

    // Verify that the namespace with same id does not already exist, except in the case of ysql
    // major catalog upgrade, in which case it will be reused.
    ns = FindPtrOrNull(namespace_ids_map_, req->namespace_id());
    RETURN_NOT_OK(check_ns_errors(ns, true /* by_id */));

    // Use the by name namespace map to enforce global uniqueness for both YCQL keyspaces and YSQL
    // databases.  Although postgres metadata normally enforces db name uniqueness, it fails in
    // case of concurrent CREATE DATABASE requests in different sessions.
    ns = FindPtrOrNull(namespace_names_mapper_[db_type], req->name());
    RETURN_NOT_OK(check_ns_errors(ns, false /* by_id */));

    // Add the new namespace.

    // Create unique id for this new namespace, unless it already exists in the online ysql major
    // version catalog upgrade case.
    if (!IsYsqlMajorCatalogUpgradeInProgress()) {
      NamespaceId new_id = !req->namespace_id().empty()
                               ? req->namespace_id()
                               : GenerateIdUnlocked(SysRowEntryType::NAMESPACE);
      ns = new NamespaceInfo(new_id, tasks_tracker_);
    }
    ns_l = ns->LockForWrite();
    SysNamespaceEntryPB *metadata = &ns_l.mutable_data()->pb;
    metadata->set_name(req->name());
    metadata->set_database_type(db_type);
    metadata->set_colocated(req->colocated());
    // Note that during online ysql major version catalog upgrade, a namespace whose current-version
    // catalogs are being prepared will switch into state PREPARING. This is safe because DDLs are
    // not allowed during the upgrade.
    metadata->set_state(SysNamespaceEntryPB::PREPARING);

    // For namespace created for a Postgres database, save the list of tables and indexes for
    // for the database that need to be copied.
    if (db_type == YQL_DATABASE_PGSQL) {
      if (req->source_namespace_id().empty()) {
        metadata->set_next_pg_oid(req->next_pg_oid());
      } else {
        const auto source_oid = GetPgsqlDatabaseOid(req->source_namespace_id());
        if (!source_oid.ok()) {
          return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND,
                            source_oid.status());
        }
        for (const auto& table : tables_->GetAllTables()) {
          // The expectation is that we're copying "all" of the PG tables from the source namespace
          // to the target namespace. There are 3 situations in which we copy a namespace:
          //  1) initdb, during a clean install
          //  2) initdb, running as part of a ysql major catalog upgrade
          //  3) steady state in either case (post-universe-creation, or post-upgrade)
          //
          // What does "all" mean in these cases?
          //  * In case #1, it means all PG tables, though they will all be current-version catalog
          //    tables.
          //  * In case #2, it means only current-version system catalog tables, because prior-
          //    version catalog tables and user tables (which do not have a version) have already
          //    been added to the namespace previously.
          //  * In case #3, it means all PG tables. Valid tables are either current-version catalog
          //    tables, or user tables (which do not have a version). Any prior-version catalog
          //    tables are around just because they haven't been cleaned up yet.
          //
          // So in case #2, we must accept only current-version catalog tables.
          const auto& table_id = table->id();
          if (IsPgsqlId(table_id) && VERIFY_RESULT(GetPgsqlDatabaseOid(table_id)) == *source_oid) {
            if (IsPriorVersionYsqlCatalogTable(table_id)) {
              continue;
            }
            // Since indexes have dependencies on the base tables, put the tables in the front.
            const bool is_table = table->indexed_table_id().empty();
            pgsql_tables.insert(is_table ? pgsql_tables.begin() : pgsql_tables.end(), table);
          }
        }

        scoped_refptr<NamespaceInfo> source_ns = FindPtrOrNull(namespace_ids_map_,
                                                               req->source_namespace_id());
        if (!source_ns) {
          return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND,
                            STATUS(NotFound, "Source keyspace not found",
                                   req->source_namespace_id()));
        }
        if (FLAGS_ysql_enable_pg_per_database_oid_allocator) {
          metadata->set_next_pg_oid(kPgFirstNormalObjectId);
        } else {
          auto source_ns_lock = source_ns->LockForRead();
          metadata->set_next_pg_oid(source_ns_lock->pb.next_pg_oid());
        }
      }
    }

    // NS with a Transaction should be rolled back if the transaction does not get Committed.
    // Store this on the NS for now and use it later.
    if (req->has_transaction() && PREDICT_TRUE(FLAGS_enable_transactional_ddl_gc)) {
      metadata->mutable_transaction()->CopyFrom(req->transaction());
      txn = VERIFY_RESULT(TransactionMetadata::FromPB(req->transaction()));
      RSTATUS_DCHECK(!txn.status_tablet.empty(), Corruption, "Given incomplete Transaction");
    }

    // Add the namespace to the in-memory map for the assignment.
    namespace_ids_map_[ns->id()] = ns;
    namespace_names_mapper_[db_type][req->name()] = ns;

    resp->set_id(ns->id());
  }
  TRACE("Inserted new keyspace info into CatalogManager maps");

  // Update the on-disk system catalog.
  return_status = sys_catalog_->Upsert(leader_ready_term(), ns);
  if (!return_status.ok()) {
    LOG(WARNING) << "Keyspace creation failed:" << return_status.ToString();
    {
      LockGuard lock(mutex_);
      namespace_ids_map_.erase(ns->id());
      namespace_names_mapper_[db_type].erase(req->name());
    }
    return CheckIfNoLongerLeaderAndSetupError(return_status, resp);
  }
  TRACE("Wrote keyspace to sys-catalog");
  // Commit the namespace in-memory state.
  ns_l.Commit();

  LOG(INFO) << "Created keyspace " << ns->ToString();

  if (req->has_creator_role_name()) {
    RETURN_NOT_OK(permissions_manager_->GrantPermissions(
        req->creator_role_name(),
        get_canonical_keyspace(req->name()),
        req->name() /* resource name */,
        req->name() /* keyspace name */,
        all_permissions_for_resource(ResourceType::KEYSPACE),
        ResourceType::KEYSPACE,
        resp));
  }

  // Pre-Colocation GA colocated databases need to create a parent tablet to serve as the base
  // storage location along with creation of the colocated database.
  if (PREDICT_TRUE(FLAGS_ysql_legacy_colocated_database_creation) && req->colocated()) {
    CreateTableRequestPB req;
    CreateTableResponsePB resp;
    const auto parent_table_id = GetColocatedDbParentTableId(ns->id());
    const auto parent_table_name = GetColocatedDbParentTableName(ns->id());
    req.set_name(parent_table_name);
    req.set_table_id(parent_table_id);
    req.mutable_namespace_()->set_name(ns->name());
    req.mutable_namespace_()->set_id(ns->id());
    req.set_table_type(GetTableTypeForDatabase(ns->database_type()));
    req.set_is_colocated_via_database(true);

    YBSchemaBuilder schema_builder;
    schema_builder.AddColumn("parent_column")->Type(DataType::BINARY)->PrimaryKey();
    YBSchema ybschema;
    CHECK_OK(schema_builder.Build(&ybschema));
    auto schema = yb::client::internal::GetSchema(ybschema);
    SchemaToPB(schema, req.mutable_schema());
    req.mutable_schema()->mutable_table_properties()->set_is_transactional(true);

    // create a parent table, which will create the tablet.
    Status s = CreateTable(&req, &resp, rpc, epoch);
    // We do not lock here so it is technically possible that the table was already created.
    // If so, there is nothing to do so we just ignore the "AlreadyPresent" error.
    if (!s.ok() && !s.IsAlreadyPresent()) {
      LOG(WARNING) << "Keyspace creation failed:" << s.ToString();
      // TODO: We should verify this behavior works end-to-end.
      // Diverging in-memory state from disk so the user can issue a delete if no new leader.
      auto l = ns->LockForWrite();
      SysNamespaceEntryPB& metadata = ns->mutable_metadata()->mutable_dirty()->pb;
      metadata.set_state(SysNamespaceEntryPB::FAILED);
      l.Commit();
      return s;
    }
  }

  if (db_type == YQL_DATABASE_PGSQL) {
    LOG(INFO) << "Keyspace create enqueued for later processing: " << ns->ToString();
    RETURN_NOT_OK(background_tasks_thread_pool_->SubmitFunc(std::bind(
        &CatalogManager::ProcessPendingNamespace, this, ns->id(), pgsql_tables, txn, epoch)));
    return Status::OK();
  } else {
    auto l = ns->LockForWrite();
    SysNamespaceEntryPB& metadata = ns->mutable_metadata()->mutable_dirty()->pb;
    if (metadata.state() == SysNamespaceEntryPB::PREPARING) {
      metadata.set_state(SysNamespaceEntryPB::RUNNING);
      return_status = sys_catalog_->Upsert(leader_ready_term(), ns);
      if (!return_status.ok()) {
        // Diverging in-memory state from disk so the user can issue a delete if no new leader.
        LOG(WARNING) << "Keyspace creation failed:" << return_status.ToString();
        metadata.set_state(SysNamespaceEntryPB::FAILED);
        return_status = CheckIfNoLongerLeaderAndSetupError(return_status, resp);
      } else {
        TRACE("Activated keyspace in sys-catalog");
        LOG(INFO) << "Activated keyspace: " << ns->ToString();
      }
      // Commit the namespace in-memory state.
      l.Commit();
    } else {
      LOG(WARNING) << "Keyspace has invalid state (" << metadata.state() << "), aborting create";
    }
  }
  return return_status;
}

void CatalogManager::ProcessPendingNamespace(
    NamespaceId id,
    std::vector<scoped_refptr<TableInfo>> template_tables,
    TransactionMetadata txn, const LeaderEpoch& epoch) {
  LOG(INFO) << "ProcessPendingNamespace started for " << id;

  // Ensure that we are the leader and our view of the term does not change while handling DDL
  // operations (to avoid losing and regaining leadership, and having the catalog loaders run).
  SCOPED_LEADER_SHARED_LOCK(l, this);
  if (!l.IsInitializedAndIsLeader()) {
    LOG(WARNING) << l.failed_status_string();
    // Don't try again, we have to reset in-memory state after losing leader election.
    return;
  }
  auto term = leader_ready_term();

  if (PREDICT_FALSE(GetAtomicFlag(&FLAGS_TEST_hang_on_namespace_transition))) {
    TEST_SYNC_POINT("CatalogManager::ProcessPendingNamespace:Fail");
    LOG(INFO) << "Artificially waiting (" << FLAGS_catalog_manager_bg_task_wait_ms
              << "ms) on namespace creation for " << id;
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_catalog_manager_bg_task_wait_ms));
    WARN_NOT_OK(
        background_tasks_thread_pool_->SubmitFunc(std::bind(
            &CatalogManager::ProcessPendingNamespace, this, id, template_tables, txn, epoch)),
        "Could not submit ProcessPendingNamespaces to thread pool");
    return;
  }

  auto ns_res = FindNamespaceById(id);
  if (!ns_res.ok()) {
    LOG(WARNING) << Format(
        "Pending namespace with id $0 not found in ids map, cannot finish namespace creation",
        id);
    return;
  }
  auto& ns = **ns_res;

  // Copy the system tables necessary to create this namespace.  This can be time-intensive.
  Status status = CopyPgsqlSysTables(ns, template_tables, epoch);
  // All work is done, change the namespace state regardless of success or failure.
  auto ns_write_lock = ns.LockForWrite();
  SysNamespaceEntryPB& metadata = ns_write_lock.mutable_data()->pb;
  if (metadata.state() != SysNamespaceEntryPB::PREPARING) {
    LOG(DFATAL) << "Bad keyspace state (" << metadata.state() << "), abandoning creation work for "
                << ns.ToString();
    return;
  }
  auto cleanup = ScopeExit([this, &status, &ns_write_lock, &ns, &metadata] {
    if (status.ok()) return;
    TRACE("Handling failed keyspace creation");
    // Do not set on-disk state here. The loader treats the PREPARING state as FAILED.
    if (IsYsqlMajorCatalogUpgradeInProgress()) {
      metadata.set_ysql_next_major_version_state(SysNamespaceEntryPB::NEXT_VER_FAILED);
    } else {
      metadata.set_state(SysNamespaceEntryPB::FAILED);
    }
    ns_write_lock.Commit();
    LOG(WARNING) << status.ToString();
    LockGuard lock(mutex_);
    // Remove entry from the by-name map. For YSQL postgres clients cannot issue a DROP
    // DATABASE command at this point because postgres will not commit the metadata for this
    // database to its catalogs. So allow users to create a database with the same name.
    namespace_names_mapper_[ns.database_type()].erase(ns.name());
  });

  if (!status.ok()) {
    status = status.CloneAndPrepend("Error copying PGSQL system tables for pending namespace");
    return;
  }

  metadata.set_state(SysNamespaceEntryPB::RUNNING);
  metadata.set_ysql_next_major_version_state(SysNamespaceEntryPB::NEXT_VER_RUNNING);
  status = sys_catalog_->Upsert(term, &ns);
  if (!status.ok()) {
    status = status.CloneAndPrepend(Format(
        "An error occurred while modifying keyspace to $0 in sys-catalog", metadata.state()));
    return;
  }
  TRACE("Done processing keyspace");
  LOG(INFO) << "Processed keyspace: " << ns.ToString();
  auto has_transaction = metadata.has_transaction();
  ns_write_lock.Commit();
  if (has_transaction) {
    LOG(INFO) << "Enqueuing keyspace for Transaction Verification: " << ns.ToString();
    ScheduleVerifyNamespacePgLayer(txn, *ns_res, epoch);
  }
}

void CatalogManager::ScheduleVerifyNamespacePgLayer(
    TransactionMetadata txn, NamespaceInfoPtr ns, const LeaderEpoch& epoch) {
  auto when_done = [this, ns, epoch](Result<bool> result) {
    WARN_NOT_OK(VerifyNamespacePgLayer(ns, result, epoch), "VerifyNamespacePgLayer");
  };
  NamespaceVerificationTask::CreateAndStartTask(
      *this, ns, txn, std::move(when_done), sys_catalog_, master_->client_future(),
      *master_->messenger(), epoch);
}

Status CatalogManager::VerifyNamespacePgLayer(scoped_refptr<NamespaceInfo> ns,
                                              Result<bool> exists,
                                              const LeaderEpoch& epoch) {
  if (!exists.ok()) {
    return exists.status();
  }
  auto l = ns->LockForWrite();
  SysNamespaceEntryPB& metadata = ns->mutable_metadata()->mutable_dirty()->pb;

  if (exists.get()) {
    // Passed checks.  Remove the transaction from the entry since we're done processing it.
    SCHECK_EQ(metadata.state(), SysNamespaceEntryPB::RUNNING, Aborted,
              Format("Invalid Namespace state ($0), abandoning transaction GC work for $1",
                 SysNamespaceEntryPB_State_Name(metadata.state()), ns->ToString()));
    metadata.clear_transaction();
    RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), ns));
    LOG(INFO) << "Namespace transaction succeeded: " << ns->ToString();
    // Commit the namespace in-memory state.
    l.Commit();
  } else {
    // Transaction failed.  We need to delete this Database now.
    SCHECK(metadata.state() == SysNamespaceEntryPB::RUNNING ||
           metadata.state() == SysNamespaceEntryPB::FAILED, Aborted,
           Format("Invalid Namespace state ($0), aborting delete.",
                      SysNamespaceEntryPB_State_Name(metadata.state()), ns->ToString()));
    LOG(INFO) << "Namespace transaction failed, deleting: " << ns->ToString();
    metadata.set_state(SysNamespaceEntryPB::DELETING);
    metadata.clear_transaction();
    // todo(zdrudi): we seem to name squat here. The failed creation of a db is visible to all
    // clients because the db's name is still in the map, preventing creation of a new db with the
    // same name. If the async database cleanup fails then we leak the name until restart.  We
    // should probably remove the name from the map here, but it's not clear what to do with this db
    // if we restart without committing the write below.
    RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), ns));
    // Commit the namespace in-memory state.
    l.Commit();
    // Async enqueue delete.
    RETURN_NOT_OK(background_tasks_thread_pool_->SubmitFunc(
        std::bind(&CatalogManager::DeleteYsqlDatabaseAsync, this, ns, epoch)));
  }
  return Status::OK();
}

// Get the information about an in-progress create operation.
Status CatalogManager::IsCreateNamespaceDone(const IsCreateNamespaceDoneRequestPB* req,
                                             IsCreateNamespaceDoneResponsePB* resp) {
  auto ns_pb = req->namespace_();

  // 1. Lookup the namespace and verify it exists.
  TRACE("Looking up keyspace");
  auto ns = VERIFY_NAMESPACE_FOUND(FindNamespace(ns_pb), resp);

  TRACE("Locking keyspace");
  auto l = ns->LockForRead();
  auto metadata = l->pb;

  switch (metadata.state()) {
    // Success cases. Done and working.
    case SysNamespaceEntryPB::RUNNING:
      if (PREDICT_FALSE(!FLAGS_ysql_legacy_colocated_database_creation) || !ns->colocated()) {
        resp->set_done(true);
      } else {
        // Verify system table created as well, if colocated.
        IsCreateTableDoneRequestPB table_req;
        IsCreateTableDoneResponsePB table_resp;
        const auto parent_table_id = GetColocatedDbParentTableId(ns->id());
        table_req.mutable_table()->set_table_id(parent_table_id);
        auto s = IsCreateTableDone(&table_req, &table_resp);
        resp->set_done(table_resp.done());
        if (!s.ok()) {
          if (table_resp.has_error()) {
            resp->mutable_error()->Swap(table_resp.mutable_error());
          }
          return s;
        }
      }
      break;
    // These states indicate that a create completed but a subsequent remove was requested.
    case SysNamespaceEntryPB::DELETING:
    case SysNamespaceEntryPB::DELETED:
      resp->set_done(true);
      if (ns->database_type() == YQL_DATABASE_PGSQL) {
        return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR,
            STATUS(InternalError,
                "Namespace Create Failed: The namespace is in process "
                "of deletion due to internal error."));
      }
      break;
    // Pending cases.  NOT DONE
    case SysNamespaceEntryPB::PREPARING:
      resp->set_done(false);
      break;
    // Failure cases.  Done, but we need to give the user an error message.
    case SysNamespaceEntryPB::FAILED:
      resp->set_done(true);
      return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, STATUS(InternalError,
              "Namespace Create Failed: not onlined."));
    default:
      Status s = STATUS_SUBSTITUTE(IllegalState, "IsCreateNamespaceDone failure: state=$0",
                                   SysNamespaceEntryPB_State_Name(metadata.state()));
      LOG(WARNING) << s.ToString();
      resp->set_done(true);
      return SetupError(resp->mutable_error(), MasterErrorPB::UNKNOWN_ERROR, s);
  }

  return Status::OK();
}

Status CatalogManager::DeleteNamespace(const DeleteNamespaceRequestPB* req,
                                       DeleteNamespaceResponsePB* resp,
                                       rpc::RpcContext* rpc,
                                       const LeaderEpoch& epoch) {
  auto status = DoDeleteNamespace(req, resp, rpc, epoch);
  if (!status.ok()) {
    return SetupError(resp->mutable_error(), status);
  }
  return status;
}

Status CatalogManager::CheckIfDatabaseHasReplication(const scoped_refptr<NamespaceInfo>& database) {
  SharedLock lock(mutex_);
  for (const auto& table : tables_->GetAllTables()) {
    auto ltm = table->LockForRead();
    if (ltm->namespace_id() != database->id() || ltm->started_deleting()) {
      continue;
    }
    if (xcluster_manager_->IsTableReplicated(table->id())) {
      LOG(ERROR) << "Error deleting database: " << database->id() << ", table: " << table->id()
                 << " is under replication"
                 << ". Cannot delete a database that contains tables under replication.";
      return STATUS_FORMAT(
          InvalidCommand, Format(
                              "Table: $0 is under replication. Cannot delete a database that "
                              "contains tables under replication.",
                              table->id()));
    }
  }
  return Status::OK();
}

Status CatalogManager::DoDeleteNamespace(const DeleteNamespaceRequestPB* req,
                                         DeleteNamespaceResponsePB* resp,
                                         rpc::RpcContext* rpc,
                                         const LeaderEpoch& epoch) {
  LOG(INFO) << "Servicing DeleteNamespace request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  // Lookup the namespace and verify if it exists.
  TRACE("Looking up keyspace");
  auto ns = VERIFY_RESULT(FindNamespace(req->namespace_()));

  if (req->has_database_type() && req->database_type() != ns->database_type()) {
    // Could not find the right database to delete.
    return STATUS(NotFound, "Keyspace not found", ns->name(),
                  MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
  }
  {
    // Don't allow deletion if the namespace is in a transient state.
    auto cur_state = ns->state();
    if (IsYsqlMajorCatalogUpgradeInProgress()) {
      // Note that during a ysql major catalog upgrade, deleting a namespace is only done by the
      // upgrade process, and only "deletes" the new major version's namespace. In Yugabyte, we
      // track the deleted state, and delete the new major version's catalog tables, without
      // deleting the namespace itself, or touching the old major version's catalog tables or any
      // user tables.
      SCHECK_FORMAT(cur_state == SysNamespaceEntryPB::RUNNING, IllegalState,
                    "Deleting the namespace for the new YSQL version isn't allowed if the primary "
                    "state of the namespace isn't RUNNING. Current state is $0",
                    SysNamespaceEntryPB::State_Name(cur_state));
      // Note that if the ysql next major version state enters state FAILED, we would expect the
      // user to roll back (deleting all signs of the new ysql major catalog) before trying
      // another ysql major catalog upgrade.
      auto cur_ysql_next_major_version_state = ns->ysql_next_major_version_state();
      SCHECK_FORMAT(cur_ysql_next_major_version_state == SysNamespaceEntryPB::NEXT_VER_RUNNING,
                    IllegalState,
                    "Deleting the namespace for the new YSQL version isn't allowed if the upgrade "
                    "state isn't RUNNING. Current upgrade state is $0",
                    SysNamespaceEntryPB::YsqlNextMajorVersionState_Name(
                        cur_ysql_next_major_version_state));
    } else if (
        cur_state != SysNamespaceEntryPB::RUNNING && cur_state != SysNamespaceEntryPB::FAILED) {
      if (cur_state == SysNamespaceEntryPB::DELETED) {
        return STATUS(NotFound, "Keyspace already deleted", ns->name(),
                      MasterError(MasterErrorPB::NAMESPACE_NOT_FOUND));
      } else {
        return STATUS_EC_FORMAT(
            TryAgain, MasterError(MasterErrorPB::IN_TRANSITION_CAN_RETRY),
            "Namespace deletion not allowed when State = $0",
            SysNamespaceEntryPB::State_Name(cur_state));
      }
    }
  }

  // PGSQL has a completely forked implementation because it allows non-empty namespaces on delete.
  if (ns->database_type() == YQL_DATABASE_PGSQL) {
    return DeleteYsqlDatabase(req, resp, rpc, epoch);
  }

  TRACE("Locking keyspace");
  auto l = ns->LockForWrite();

  // Only empty namespace can be deleted.
  TRACE("Looking for tables in the keyspace");
  {
    SharedLock lock(mutex_);
    VLOG_WITH_FUNC(3) << "Acquired the catalog manager lock";

    for (const auto& table : tables_->GetAllTables()) {
      auto ltm = table->LockForRead();

      if (!ltm->started_deleting() && ltm->namespace_id() == ns->id()) {
        return STATUS_EC_FORMAT(
            InvalidArgument, MasterError(MasterErrorPB::NAMESPACE_IS_NOT_EMPTY),
            "Cannot delete keyspace which has $0: $1 [id=$2], request: $3",
            IsTable(ltm->pb) ? "table" : "index", ltm->name(), table->id(),
            req->ShortDebugString());
      }
    }

    // Only empty namespace can be deleted.
    TRACE("Looking for types in the keyspace");

    for (const UDTypeInfoMap::value_type& entry : udtype_ids_map_) {
      auto ltm = entry.second->LockForRead();

      if (ltm->namespace_id() == ns->id()) {
        return STATUS_EC_FORMAT(
            InvalidArgument, MasterError(MasterErrorPB::NAMESPACE_IS_NOT_EMPTY),
            "Cannot delete keyspace which has type: $0 [id=$1], request: $2",
            ltm->name(), entry.second->id(), req->ShortDebugString());
      }
    }
  }

  // Disallow deleting namespaces with snapshot schedules.
  auto covering_schedules = VERIFY_RESULT(
      master_->snapshot_coordinator().GetSnapshotSchedules(
          SysRowEntryType::NAMESPACE, ns->id()));
  if (!covering_schedules.empty()) {
    return STATUS_EC_FORMAT(
        InvalidArgument, MasterError(MasterErrorPB::NAMESPACE_IS_NOT_EMPTY),
        "Cannot delete keyspace which has schedule: $0, request: $1",
        AsString(covering_schedules), req->ShortDebugString());
  }

  // [Delete]. Skip the DELETING->DELETED state, since no tables are present in this namespace.
  TRACE("Updating metadata on disk");
  // Update sys-catalog.
  Status s = sys_catalog_->Delete(leader_ready_term(), ns);
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend("An error occurred while updating sys-catalog");
    LOG(WARNING) << s;
    return CheckIfNoLongerLeader(s);
  }

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l.Commit();

  // todo: Could something with a cached id or name of this namespace try a lookup here and get a
  // RUNNING namespace that we've removed from the sys catalog?

  // Remove the namespace from all CatalogManager mappings.
  {
    LockGuard lock(mutex_);
    if (namespace_names_mapper_[ns->database_type()].erase(ns->name()) < 1) {
      LOG(WARNING) << Format("Could not remove namespace from names map, id=$1", ns->id());
    }
    if (namespace_ids_map_.erase(ns->id()) < 1) {
      LOG(WARNING) << Format("Could not remove namespace from ids map, id=$1", ns->id());
    }
  }

  // Delete any permissions granted on this keyspace to any role. See comment in DeleteTable() for
  // more details.
  string canonical_resource = get_canonical_keyspace(req->namespace_().name());
  RETURN_NOT_OK(permissions_manager_->RemoveAllPermissionsForResource(canonical_resource, resp));

  LOG(INFO) << "Successfully deleted keyspace " << ns->ToString()
            << " per request from " << RequestorString(rpc);
  return Status::OK();
}

// N.B. This function is not called by the catalog loader. If there are mutations or checks
// that should be performed before the loaders delete a ysql database they should be put into
// DeleteYsqlDatabaseAsync instead of here.
Status CatalogManager::DeleteYsqlDatabase(const DeleteNamespaceRequestPB* req,
                                          DeleteNamespaceResponsePB* resp,
                                          rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  // Lookup database.
  auto database = VERIFY_NAMESPACE_FOUND(FindNamespace(req->namespace_()), resp);

  // Make sure this is a YSQL database.
  if (database->database_type() != YQL_DATABASE_PGSQL) {
    // A non-YSQL namespace is found, but the rpc requests to drop a YSQL database.
    Status s = STATUS(NotFound, "YSQL database not found", database->name());
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
  }

  // Disallow deleting namespaces with snapshot schedules.
  auto covering_schedules = VERIFY_RESULT(
      master_->snapshot_coordinator().GetSnapshotSchedules(
          SysRowEntryType::NAMESPACE, database->id()));
  if (!covering_schedules.empty()) {
    return STATUS_EC_FORMAT(
        InvalidArgument, MasterError(MasterErrorPB::NAMESPACE_IS_NOT_EMPTY),
        "Cannot delete database which has schedule: $0, request: $1",
        AsString(covering_schedules), req->ShortDebugString());
  }

  // Only allow YSQL database deletion if it does not contain any replicated tables. No need to
  // check this for YCQL keyspaces, as YCQL does not allow drops of non-empty keyspaces, regardless
  // of their replication status.
  RETURN_NOT_OK(CheckIfDatabaseHasReplication(database));

  // Set the Namespace to DELETING.
  TRACE("Locking database");
  auto l = database->LockForWrite();
  SysNamespaceEntryPB& metadata = database->mutable_metadata()->mutable_dirty()->pb;
  if (IsYsqlMajorCatalogUpgradeInProgress()) {
    if (metadata.state() != SysNamespaceEntryPB::RUNNING) {
      return STATUS_EC_FORMAT(IllegalState, MasterError(MasterErrorPB::INTERNAL_ERROR),
                              "Keyspace ($0) has invalid state ($1), aborting delete",
                              database->name(), metadata.state());
    }
    if (metadata.ysql_next_major_version_state() != SysNamespaceEntryPB::NEXT_VER_RUNNING) {
      return STATUS_EC_FORMAT(IllegalState, MasterError(MasterErrorPB::INTERNAL_ERROR),
                              "Keyspace ($0) has invalid ysql next major version state ($1), "
                              "aborting delete",
                              database->name(), metadata.ysql_next_major_version_state());
    }
  } else if (
      metadata.state() != SysNamespaceEntryPB::RUNNING &&
      metadata.state() != SysNamespaceEntryPB::FAILED) {
    return SetupError(
        resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR,
        STATUS_SUBSTITUTE(
            IllegalState, "Keyspace ($0) has invalid state ($1), aborting delete", database->name(),
            metadata.state()));
  }
  if (IsYsqlMajorCatalogUpgradeInProgress()) {
    // During a ysql major catalog upgrade, the upstream Postgres mechanism drops and re-creates
    // system namespaces. Therefore, we keep a separate state machine for the status of the new
    // major version's namespace. It starts at RUNNING when called from initdb, and then is later
    // "deleted" and then "re-created" by the upgrade process.
    metadata.set_ysql_next_major_version_state(SysNamespaceEntryPB::NEXT_VER_DELETING);
  } else {
    metadata.set_state(SysNamespaceEntryPB::DELETING);
  }
  RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), database));
  TRACE("Marked keyspace for deletion in sys-catalog");
  l.Commit();
  return background_tasks_thread_pool_->SubmitFunc(
      std::bind(&CatalogManager::DeleteYsqlDatabaseAsync, this, database, epoch));
}

void CatalogManager::DeleteYsqlDatabaseAsync(
    scoped_refptr<NamespaceInfo> database, const LeaderEpoch& epoch) {
  TEST_PAUSE_IF_FLAG(TEST_hang_on_namespace_transition);

  const auto is_ysql_major_upgrade = IsYsqlMajorCatalogUpgradeInProgress();

  // Lock database before removing content.
  TRACE("Locking database");
  auto l = database->LockForWrite();
  SysNamespaceEntryPB &metadata = database->mutable_metadata()->mutable_dirty()->pb;

  // A DELETED Namespace has finished but was tombstoned to avoid immediately reusing the same ID.
  // We consider a restart enough time, so we just need to remove it from the SysCatalog.
  if (metadata.state() == SysNamespaceEntryPB::DELETED) {
    Status s = sys_catalog_->Delete(leader_ready_term(), database);
    WARN_NOT_OK(s, "SysCatalog DeleteItem for Namespace");
    if (!s.ok()) {
      return;
    }
  }

  if (is_ysql_major_upgrade) {
    if (metadata.state() != SysNamespaceEntryPB::RUNNING) {
      LOG(DFATAL) << "Namespace (" << database->name() << ") has invalid state "
                  << SysNamespaceEntryPB::State_Name(metadata.state());
      return;
    }
    if (metadata.ysql_next_major_version_state() != SysNamespaceEntryPB::NEXT_VER_DELETING) {
      LOG(DFATAL) << "Namespace (" << database->name()
                  << ") has invalid ysql next major version state "
                  << SysNamespaceEntryPB::YsqlNextMajorVersionState_Name(
                         metadata.ysql_next_major_version_state());
      return;
    }
  } else if (metadata.state() != SysNamespaceEntryPB::DELETING) {
    LOG(WARNING) << "Keyspace (" << database->name() << ") has invalid state (" << metadata.state()
                 << "), aborting delete";
    return;
  }

  // Delete all tables in the database. If we're in a ysql major catalog upgrade, this will delete
  // only the new version's tables.
  TRACE("Delete all tables in YSQL database");
  Status s = DeleteYsqlDBTables(
      database,
      /*is_for_ysql_major_rollback=*/false, epoch);
  WARN_NOT_OK(s, "DeleteYsqlDBTables failed");
  if (!s.ok()) {
    if (is_ysql_major_upgrade) {
      metadata.set_ysql_next_major_version_state(SysNamespaceEntryPB::NEXT_VER_FAILED);
    } else {
      // Move to FAILED so DeleteNamespace can be reissued by the user.
      metadata.set_state(SysNamespaceEntryPB::FAILED);
    }
    l.Commit();
    return;
  }

  // Once all user-facing data has been offlined, move the Namespace to DELETED state.
  if (is_ysql_major_upgrade) {
    metadata.set_ysql_next_major_version_state(SysNamespaceEntryPB::NEXT_VER_DELETED);
  } else {
    metadata.set_state(SysNamespaceEntryPB::DELETED);
  }
  s = sys_catalog_->Upsert(leader_ready_term(), database);
  WARN_NOT_OK(s, "SysCatalog Update for Namespace");
  if (!s.ok()) {
    // Move to FAILED so DeleteNamespace can be reissued by the user.
    if (is_ysql_major_upgrade) {
      metadata.set_ysql_next_major_version_state(SysNamespaceEntryPB::NEXT_VER_FAILED);
    } else {
      metadata.set_state(SysNamespaceEntryPB::FAILED);
    }
    l.Commit();
    return;
  }
  TRACE("Marked keyspace as deleted in sys-catalog");

  // During an upgrade, we skip the actual namespace deletion.
  if (is_ysql_major_upgrade) {
    LOG(INFO) << "We're in a ysql major catalog upgrade, skipping actual namespace deletion";
    l.Commit();
    return;
  }

  // Remove namespace from CatalogManager name mapping.
  {
    LockGuard lock(mutex_);
    auto it = namespace_names_mapper_[database->database_type()].find(database->name());
    if (it == namespace_names_mapper_[database->database_type()].end()) {
      // Because we remove YSQL namespaces whose async creation failed from the maps,
      // for such databases this is an expected error.
      LOG(WARNING) << Format(
          "Could not remove namespace from maps, name=$0, id=$1", database->name(), database->id());
    } else if (it->second->id() == database->id()) {
      // Sanity check we're not removing the wrong database. We don't enforce name uniqueness for
      // databases whose creation failed.
      namespace_names_mapper_[database->database_type()].erase(database->name());
    } else {
      LOG(WARNING) << Format(
          "While removing namespace of type $0 with id $1 and name $2 found a different namespace "
          "in the names map under the same name, with id $3",
          YQLDatabase_Name(database->database_type()), database->id(), database->name(),
          it->second->id());
    }
  }
  TRACE("Committing in-memory state");
  l.Commit();

  // DROP completed. Return status.
  LOG(INFO) << "Successfully deleted YSQL database " << database->ToString();
}

// IMPORTANT: If modifying, consider updating DeleteTable(), the singular deletion API.
Status CatalogManager::DeleteYsqlDBTables(
    const scoped_refptr<NamespaceInfo>& database, const bool is_for_ysql_major_rollback,
    const LeaderEpoch& epoch) {
  const auto is_ysql_major_upgrade = IsYsqlMajorCatalogUpgradeInProgress();
  if (is_for_ysql_major_rollback) {
    RSTATUS_DCHECK(
        is_ysql_major_upgrade, IllegalState,
        "DeleteYsqlDBTables called with is_for_ysql_major_upgrade when not in a YSQL major catalog "
        "upgrade");
  }
  TabletInfoPtr sys_tablet_info;
  vector<pair<scoped_refptr<TableInfo>, TableInfo::WriteLock>> tables_and_locks;
  std::unordered_set<TableId> sys_table_ids;
  {
    // Lock the catalog to iterate over table_ids_map_.
    SharedLock lock(mutex_);

    sys_tablet_info = tablet_map_->find(kSysCatalogTabletId)->second;

    vector<pair<scoped_refptr<TableInfo>, TableInfo::WriteLock>> colocation_parents;

    // Populate tables and sys_table_ids.
    for (const auto& table : tables_->GetAllTables()) {
      // In ysql major catalog upgrade mode, there are two possibilities:
      //  * To propagate database-level properties for certain databases in an upgrade, pg_upgrade
      //    drops those databases and recreates them. We pretend to do so, but only delete the
      //    current version's catalog tables, so the restore portion doesn't get confused.
      //  * The rollback deletes all current-version catalog tables in order to prepare for the next
      //    upgrade attempt.
      // In both cases, we delete only the current version's catalog tables.
      if (is_ysql_major_upgrade && !IsCurrentVersionYsqlCatalogTable(table->id())) {
        continue;
      }
      if (table->namespace_id() != database->id()) {
        continue;
      }
      // todo(zdrudi): we're acquiring table locks out of order here.
      auto l = table->LockForWrite();
      if (l->started_deleting()) {
        continue;
      }

      // During the YSQL major catalog upgrade, don't drop shared tables for drop database (see the
      // comment at the beginning of the for loop), because such tables are technically global
      // tables, not contained in template1.
      //
      // During YSQL major catalog upgrade rollback, shared tables for the current version hosted in
      // the template1 namespace must be deleted so that we return to a clean state. This is safe
      // because DDLs are disabled and there are no current-version tservers connected.
      if (is_ysql_major_upgrade) {
        if (l->pb.is_pg_shared_table() && !is_for_ysql_major_rollback) {
          continue;
        }
      } else {
        RSTATUS_DCHECK(
            !l->pb.is_pg_shared_table(), Corruption, "Shared table found in database");
      }

      if (table->is_system()) {
        sys_table_ids.insert(table->id());
      }

      // For regular (indexed) table, insert table info and lock in the front of the list. Else for
      // index table, append them to the end. We do so so that we will commit and delete the indexed
      // table first before its indexes.
      //
      // Colocation parent tables should be deleted last, so we store them separately and append to
      // the end of the list later.
      if (table->IsColocationParentTable()) {
        colocation_parents.push_back({table, std::move(l)});
      } else if (IsTable(l->pb)) {
        tables_and_locks.insert(tables_and_locks.begin(), {table, std::move(l)});
      } else {
        tables_and_locks.push_back({table, std::move(l)});
      }
    }

    if (is_ysql_major_upgrade) {
      DCHECK(colocation_parents.empty());
    }
    tables_and_locks.insert(
        tables_and_locks.end(), std::make_move_iterator(colocation_parents.begin()),
        std::make_move_iterator(colocation_parents.end()));
  }
  if (is_ysql_major_upgrade) {
    // Delete all rows from the system tables so that initdb after rollback doesn't have conflicts.
    TRACE("Deleting system table rows");
    vector<TableId> sys_table_ids_vec(sys_table_ids.begin(), sys_table_ids.end());
    RETURN_NOT_OK(
        sys_catalog_->DeleteAllYsqlCatalogTableRows(sys_table_ids_vec, epoch.leader_term));
  }
  // Remove the system tables from RAFT.
  TRACE("Sending system table delete RPCs");
  for (auto &table_id : sys_table_ids) {
    RETURN_NOT_OK(sys_catalog_->DeleteYsqlSystemTable(table_id, leader_ready_term()));
  }
  // Remove the system tables from the system catalog TabletInfo.
  RETURN_NOT_OK(RemoveTableIdsFromTabletInfo(sys_tablet_info, sys_table_ids, epoch));

  // Set all table states to DELETING as one batch RPC call.
  TRACE("Sending delete table batch RPC to sys catalog");
  std::vector<TableInfo*> table_infos;
  table_infos.reserve(tables_and_locks.size());
  for (auto& [table, l] : tables_and_locks) {
    table_infos.push_back(table.get());
    // Mark the table state as DELETING tablets.
    l.mutable_data()->set_state(SysTablesEntryPB::DELETING,
        Substitute("Started deleting at $0", LocalTimeAsString()));
  }
  // Update all the table states in raft in bulk.
  Status s = sys_catalog_->Upsert(epoch, table_infos);
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys tables: $0",
                                     s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeader(s);
  }
  for (auto& [table, l] : tables_and_locks) {
    if (l->pb.colocated() && !IsColocationParentTableId(table->id())) {
      RETURN_NOT_OK(TryRemoveFromTablegroup(table->id()));
    }

    // Cancel all table busywork and commit the DELETING change.
    l.Commit();
    table->AbortTasks();
  }

  // Batch remove all relevant CDC streams, handle after releasing Table locks.
  TRACE("Deleting CDC streams on table");
  std::unordered_set<TableId> table_ids;
  for (auto& [table, _] : tables_and_locks) {
    table_ids.insert(table->id());
  }
  RETURN_NOT_OK(DropCDCSDKStreams(table_ids));

  // Send a DeleteTablet() RPC request to each tablet replica in the table.
  for (auto& [table, _] : tables_and_locks) {
    // TODO(pitr) undelete for YSQL tables
    // We don't retain tables dropped due to a drop database even if there are
    // active snapshots or snapshot schedules covering it.
    RETURN_NOT_OK(
        DeleteOrHideTabletsOfTable(table, TabletDeleteRetainerInfo::AlwaysDelete(), epoch));
  }

  // Invoke any background tasks and return (notably, table cleanup).
  background_tasks_->Wake();
  return Status::OK();
}

// Get the information about an in-progress delete operation.
Status CatalogManager::IsDeleteNamespaceDone(const IsDeleteNamespaceDoneRequestPB* req,
                                             IsDeleteNamespaceDoneResponsePB* resp) {
  auto ns_pb = req->namespace_();

  // Lookup the namespace and verify it exists.
  TRACE("Looking up keyspace");
  auto ns = FindNamespace(ns_pb);
  if (!ns.ok()) {
    // Namespace no longer exists means success.
    LOG(INFO) << "Servicing IsDeleteNamespaceDone request for "
              << ns_pb.DebugString() << ": deleted (not found)";
    resp->set_done(true);
    return Status::OK();
  }

  TRACE("Locking keyspace");
  auto l = (**ns).LockForRead();
  auto& metadata = l->pb;

  // First, check if this is a major ysql version upgrade.
  if (IsYsqlMajorCatalogUpgradeInProgress()) {
    if (metadata.ysql_next_major_version_state() == SysNamespaceEntryPB::NEXT_VER_DELETED) {
      resp->set_done(true);
      return Status::OK();
    } else if (metadata.ysql_next_major_version_state() == SysNamespaceEntryPB::NEXT_VER_DELETING) {
      resp->set_done(false);
      return Status::OK();
    }
  }

  if (metadata.state() == SysNamespaceEntryPB::DELETED) {
    resp->set_done(true);
  } else if (metadata.state() == SysNamespaceEntryPB::DELETING) {
    resp->set_done(false);
  } else {
    Status s = STATUS_SUBSTITUTE(IllegalState,
        "Servicing IsDeleteNamespaceDone request for $0: NOT deleted (state=$1)",
        ns_pb.DebugString(), metadata.state());
    LOG(WARNING) << s.ToString();
    // Done != Successful.  We just want to let the user know the delete has finished processing.
    resp->set_done(true);
    return SetupError(resp->mutable_error(), MasterErrorPB::INTERNAL_ERROR, s);
  }
  return Status::OK();
}

Status CatalogManager::AlterNamespace(const AlterNamespaceRequestPB* req,
                                      AlterNamespaceResponsePB* resp,
                                      rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing AlterNamespace request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  auto database = VERIFY_NAMESPACE_FOUND(FindNamespace(req->namespace_()), resp);

  if (req->namespace_().has_database_type() &&
      database->database_type() != req->namespace_().database_type()) {
    Status s = STATUS(NotFound, "Database not found", database->name());
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
  }

  TRACE("Locking database");
  auto l = database->LockForWrite();

  // Don't allow an alter if the namespace isn't running.
  if (l->pb.state() != SysNamespaceEntryPB::RUNNING) {
    Status s = STATUS_SUBSTITUTE(TryAgain, "Namespace not running.  State = $0",
                                 SysNamespaceEntryPB::State_Name(l->pb.state()));
    return SetupError(resp->mutable_error(), NamespaceMasterError(l->pb.state()), s);
  }

  const string old_name = l->pb.name();

  if (req->has_new_name() && req->new_name() != old_name) {
    const string new_name = req->new_name();

    // Verify that the new name does not exist.
    NamespaceIdentifierPB ns_identifier;
    ns_identifier.set_name(new_name);
    if (req->namespace_().has_database_type()) {
      ns_identifier.set_database_type(req->namespace_().database_type());
    }
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");
    auto ns = FindNamespaceUnlocked(ns_identifier);
    if (ns.ok() && req->namespace_().has_database_type() &&
        (**ns).database_type() == req->namespace_().database_type()) {
      Status s = STATUS_SUBSTITUTE(AlreadyPresent, "Keyspace '$0' already exists", (**ns).name());
      LOG(WARNING) << "Found keyspace: " << (**ns).id() << ". Failed altering keyspace with error: "
                   << s << " Request:\n" << req->DebugString();
      return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_ALREADY_PRESENT, s);
    }

    namespace_names_mapper_[req->namespace_().database_type()][new_name] = database;
    namespace_names_mapper_[req->namespace_().database_type()].erase(old_name);

    l.mutable_data()->pb.set_name(new_name);
  }

  RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), database));

  TRACE("Committing in-memory state");
  l.Commit();

  LOG(INFO) << "Successfully altered keyspace " << req->namespace_().name()
            << " per request from " << RequestorString(rpc);
  return Status::OK();
}

Status CatalogManager::ListNamespaces(const ListNamespacesRequestPB* req,
                                      ListNamespacesResponsePB* resp) {
  NamespaceInfoMap namespace_ids_copy;
  {
    SharedLock lock(mutex_);
    namespace_ids_copy = namespace_ids_map_;
  }

  for (const auto& entry : namespace_ids_copy) {
    const auto& namespace_info = *entry.second;
    // If the request asks for namespaces for a specific database type, filter by the type.
    if (req->has_database_type() && namespace_info.database_type() != req->database_type()) {
      continue;
    }
    // Only return namespaces in state RUNNING unless list_all is true.
    if (namespace_info.state() != SysNamespaceEntryPB::RUNNING) {
      if (!req->include_nonrunning()) {
        continue;
      }
    }

    NamespaceIdentifierPB *ns = resp->add_namespaces();
    ns->set_id(namespace_info.id());
    ns->set_name(namespace_info.name());
    ns->set_database_type(namespace_info.database_type());

    resp->add_states(namespace_info.state());
    resp->add_colocated(namespace_info.colocated());
  }
  return Status::OK();
}

Status CatalogManager::GetNamespaceInfo(const GetNamespaceInfoRequestPB* req,
                                        GetNamespaceInfoResponsePB* resp,
                                        rpc::RpcContext* rpc) {
  VLOG(1) << __func__ << " from " << RequestorString(rpc) << ": " << req->ShortDebugString();

  // Look up the namespace and verify if it exists.
  TRACE("Looking up namespace");
  auto ns = VERIFY_NAMESPACE_FOUND(FindNamespace(req->namespace_()), resp);

  resp->mutable_namespace_()->set_id(ns->id());
  resp->mutable_namespace_()->set_name(ns->name());
  resp->mutable_namespace_()->set_database_type(ns->database_type());
  resp->set_colocated(ns->colocated());
  if (ns->colocated()) {
    LockGuard lock(mutex_);
    resp->set_legacy_colocated_database(colocated_db_tablets_map_.find(ns->id())
                                        != colocated_db_tablets_map_.end());
  }
  return Status::OK();
}

Status CatalogManager::RedisConfigSet(
    const RedisConfigSetRequestPB* req, RedisConfigSetResponsePB* resp, rpc::RpcContext* rpc) {
  DCHECK(req->has_keyword());
  const auto& key = req->keyword();
  SysRedisConfigEntryPB config_entry;
  config_entry.set_key(key);
  *config_entry.mutable_args() = req->args();
  bool created = false;

  TRACE("Acquired catalog manager lock");
  LockGuard lock(mutex_);
  scoped_refptr<RedisConfigInfo> cfg = FindPtrOrNull(redis_config_map_, req->keyword());
  if (cfg == nullptr) {
    created = true;
    cfg = new RedisConfigInfo(key);
    redis_config_map_[key] = cfg;
  }

  auto wl = cfg->LockForWrite();
  wl.mutable_data()->pb = std::move(config_entry);
  if (created) {
    CHECK_OK(sys_catalog_->Upsert(leader_ready_term(), cfg));
  } else {
    CHECK_OK(sys_catalog_->Upsert(leader_ready_term(), cfg));
  }
  wl.Commit();
  return Status::OK();
}

Status CatalogManager::RedisConfigGet(
    const RedisConfigGetRequestPB* req, RedisConfigGetResponsePB* resp, rpc::RpcContext* rpc) {
  DCHECK(req->has_keyword());
  resp->set_keyword(req->keyword());
  TRACE("Acquired catalog manager lock");
  SharedLock lock(mutex_);
  scoped_refptr<RedisConfigInfo> cfg = FindPtrOrNull(redis_config_map_, req->keyword());
  if (cfg == nullptr) {
    Status s = STATUS_SUBSTITUTE(NotFound, "Redis config for $0 does not exists", req->keyword());
    return SetupError(resp->mutable_error(), MasterErrorPB::REDIS_CONFIG_NOT_FOUND, s);
  }
  auto rci = cfg->LockForRead();
  resp->mutable_args()->CopyFrom(rci->pb.args());
  return Status::OK();
}

Status CatalogManager::CreateUDType(const CreateUDTypeRequestPB* req,
                                    CreateUDTypeResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  LOG(INFO) << "CreateUDType from " << RequestorString(rpc)
            << ": " << req->DebugString();

  Status s;
  scoped_refptr<UDTypeInfo> tp;
  scoped_refptr<NamespaceInfo> ns;

  // Lookup the namespace and verify if it exists.
  if (req->has_namespace_()) {
    TRACE("Looking up namespace");
    ns = VERIFY_NAMESPACE_FOUND(FindNamespace(req->namespace_()), resp);
    if (ns->database_type() != YQLDatabase::YQL_DATABASE_CQL) {
      Status s = STATUS(NotFound, "Namespace not found");
      return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
    }
  }

  // Get all the referenced types (if any).
  std::vector<std::string> referenced_udts;
  for (const QLTypePB& field_type : req->field_types()) {
    QLType::GetUserDefinedTypeIds(field_type, /* transitive = */ true, &referenced_udts);
  }

  {
    TRACE("Acquired catalog manager lock");
    LockGuard lock(mutex_);

    // Verify that the type does not exist.
    tp = FindPtrOrNull(udtype_names_map_, std::make_pair(ns->id(), req->name()));

    if (tp != nullptr) {
      resp->set_id(tp->id());
      s = STATUS_SUBSTITUTE(AlreadyPresent,
          "Type '$0.$1' already exists", ns->name(), req->name());
      LOG(WARNING) << "Found type: " << tp->id() << ". Failed creating type with error: "
                   << s.ToString() << " Request:\n" << req->DebugString();
      return SetupError(resp->mutable_error(), MasterErrorPB::TYPE_ALREADY_PRESENT, s);
    }

    // Verify that all referenced types actually exist.
    for (const auto& udt_id : referenced_udts) {
      if (FindPtrOrNull(udtype_ids_map_, udt_id) == nullptr) {
          // This may be caused by a stale cache (e.g. referenced type name resolves to an old,
          // deleted type). Return InvalidArgument so query layer will clear cache and retry.
          s = STATUS_SUBSTITUTE(InvalidArgument,
          "Type id '$0' referenced by type '$1' does not exist", udt_id, req->name());
        return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
      }
    }

    // Construct the new type (generate fresh name and set fields).
    UDTypeId new_id = GenerateIdUnlocked(SysRowEntryType::UDTYPE);
    tp = new UDTypeInfo(new_id);
    tp->mutable_metadata()->StartMutation();
    SysUDTypeEntryPB *metadata = &tp->mutable_metadata()->mutable_dirty()->pb;
    metadata->set_name(req->name());
    metadata->set_namespace_id(ns->id());
    for (const string& field_name : req->field_names()) {
      metadata->add_field_names(field_name);
    }

    for (const QLTypePB& field_type : req->field_types()) {
      metadata->add_field_types()->CopyFrom(field_type);
    }

    // Add the type to the in-memory maps.
    udtype_ids_map_[tp->id()] = tp;
    udtype_names_map_[std::make_pair(ns->id(), req->name())] = tp;
    resp->set_id(tp->id());
  }
  TRACE("Inserted new user-defined type info into CatalogManager maps");

  // Update the on-disk system catalog.
  s = sys_catalog_->Upsert(leader_ready_term(), tp);
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute(
        "An error occurred while inserting user-defined type to sys-catalog: $0", s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }
  TRACE("Wrote user-defined type to sys-catalog");

  // Commit the in-memory state.
  tp->mutable_metadata()->CommitMutation();
  LOG(INFO) << "Created user-defined type " << tp->ToString();
  return Status::OK();
}

Status CatalogManager::DeleteUDType(const DeleteUDTypeRequestPB* req,
                                    DeleteUDTypeResponsePB* resp,
                                    rpc::RpcContext* rpc) {
  LOG(INFO) << "Servicing DeleteUDType request from " << RequestorString(rpc)
            << ": " << req->ShortDebugString();

  scoped_refptr<UDTypeInfo> tp;
  scoped_refptr<NamespaceInfo> ns;

  if (!req->has_type()) {
    Status s = STATUS(InvalidArgument, "No type given", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::NAMESPACE_NOT_FOUND, s);
  }

  // Validate namespace.
  if (req->type().has_namespace_()) {
    // Lookup the namespace and verify if it exists.
    TRACE("Looking up namespace");
    ns = VERIFY_NAMESPACE_FOUND(FindNamespace(req->type().namespace_()), resp);
  }

  {
    LockGuard lock(mutex_);
    TRACE("Acquired catalog manager lock");

    if (req->type().has_type_id()) {
      tp = FindPtrOrNull(udtype_ids_map_, req->type().type_id());
    } else if (req->type().has_type_name()) {
      tp = FindPtrOrNull(udtype_names_map_, {ns->id(), req->type().type_name()});
    }

    if (tp == nullptr) {
      Status s = STATUS(NotFound, "The type does not exist", req->DebugString());
      return SetupError(resp->mutable_error(), MasterErrorPB::TYPE_NOT_FOUND, s);
    }

    // Checking if any table uses this type.
    // TODO: this could be more efficient.
    for (const auto& table : tables_->GetAllTables()) {
      auto ltm = table->LockForRead();
      if (!ltm->started_deleting()) {
        for (const auto &col : ltm->schema().columns()) {
          const Status tp_is_not_used = IterateAndDoForUDT(
              col.type(),
              [&tp](const QLTypePB::UDTypeInfo& udtype_info) -> Status {
                return udtype_info.id() == tp->id()
                    ? STATUS(QLError, Substitute("Used type $0 id $1", tp->name(), tp->id()))
                    : Status::OK();
              });

          if (!tp_is_not_used.ok()) {
            Status s = STATUS(QLError,
                Substitute("Cannot delete type '$0.$1'. It is used in column $2 of table $3",
                    ns->name(), tp->name(), col.name(), ltm->name()));
            LOG_WITH_FUNC(WARNING) << s << ": " << tp_is_not_used;
            return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
          }
        }
      }
    }

    // Checking if any other type uses this type (i.e. in the case of nested types).
    // TODO: this could be more efficient.
    for (const UDTypeInfoMap::value_type& entry : udtype_ids_map_) {
      auto ltm = entry.second->LockForRead();

      for (int i = 0; i < ltm->field_types_size(); i++) {
        // Only need to check direct (non-transitive) type dependencies here.
        // This also means we report more precise errors for in-use types.
        if (QLType::DoesUserDefinedTypeIdExist(ltm->field_types(i),
                                      false /* transitive */,
                                      tp->id())) {
          Status s = STATUS(QLError,
              Substitute("Cannot delete type '$0.$1'. It is used in field $2 of type '$3'",
                  ns->name(), tp->name(), ltm->field_names(i), ltm->name()));
          return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_REQUEST, s);
        }
      }
    }
  }

  auto l = tp->LockForWrite();

  Status s = sys_catalog_->Delete(leader_ready_term(), tp);
  if (!s.ok()) {
    // The mutation will be aborted when 'l' exits the scope on early return.
    s = s.CloneAndPrepend(Substitute("An error occurred while updating sys-catalog: $0",
        s.ToString()));
    LOG(WARNING) << s.ToString();
    return CheckIfNoLongerLeaderAndSetupError(s, resp);
  }

  // Remove it from the maps.
  {
    TRACE("Removing from maps");
    LockGuard lock(mutex_);
    if (udtype_ids_map_.erase(tp->id()) < 1) {
      PANIC_RPC(rpc, "Could not remove user defined type from map, name=" + l->name());
    }
    if (udtype_names_map_.erase({ns->id(), tp->name()}) < 1) {
      PANIC_RPC(rpc, "Could not remove user defined type from map, name=" + l->name());
    }
  }

  // Update the in-memory state.
  TRACE("Committing in-memory state");
  l.Commit();

  LOG(INFO) << "Successfully deleted user-defined type " << tp->ToString()
            << " per request from " << RequestorString(rpc);

  return Status::OK();
}

Status CatalogManager::GetUDTypeInfo(const GetUDTypeInfoRequestPB* req,
                                     GetUDTypeInfoResponsePB* resp,
                                     rpc::RpcContext* rpc) {
  LOG(INFO) << "GetUDTypeInfo from " << RequestorString(rpc)
            << ": " << req->DebugString();
  Status s;
  scoped_refptr<UDTypeInfo> tp;
  scoped_refptr<NamespaceInfo> ns;

  if (!req->has_type()) {
    s = STATUS(InvalidArgument, "Cannot get type, no type identifier given", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::TYPE_NOT_FOUND, s);
  }

  if (req->type().has_type_id()) {
    SharedLock lock(mutex_);
    tp = FindPtrOrNull(udtype_ids_map_, req->type().type_id());
  } else if (req->type().has_type_name() && req->type().has_namespace_()) {
    // Lookup the type and verify if it exists.
    TRACE("Looking up namespace");
    ns = VERIFY_NAMESPACE_FOUND(FindNamespace(req->type().namespace_()), resp);
    SharedLock lock(mutex_);
    tp = FindPtrOrNull(udtype_names_map_, std::make_pair(ns->id(), req->type().type_name()));
  }

  if (tp == nullptr) {
    s = STATUS(InvalidArgument, "Couldn't find type", req->DebugString());
    return SetupError(resp->mutable_error(), MasterErrorPB::TYPE_NOT_FOUND, s);
  }

  {
    auto type_lock = tp->LockForRead();

    UDTypeInfoPB* type_info = resp->mutable_udtype();

    type_info->set_name(tp->name());
    type_info->set_id(tp->id());
    type_info->mutable_namespace_()->set_id(type_lock->namespace_id());

    for (int i = 0; i < type_lock->field_names_size(); i++) {
      type_info->add_field_names(type_lock->field_names(i));
    }
    for (int i = 0; i < type_lock->field_types_size(); i++) {
      type_info->add_field_types()->CopyFrom(type_lock->field_types(i));
    }

    LOG(INFO) << "Retrieved user-defined type " << tp->ToString();
  }
  return Status::OK();
}

Status CatalogManager::ListUDTypes(const ListUDTypesRequestPB* req,
                                   ListUDTypesResponsePB* resp) {
  SharedLock lock(mutex_);

  // Lookup the namespace and verify that it exists.
  auto ns = VERIFY_NAMESPACE_FOUND(FindNamespaceUnlocked(req->namespace_()), resp);

  for (const UDTypeInfoByNameMap::value_type& entry : udtype_names_map_) {
    auto ltm = entry.second->LockForRead();

    // key is a pair <namespace_id, type_name>.
    if (!ns->id().empty() && ns->id() != entry.first.first) {
      continue; // Skip types from other namespaces.
    }

    UDTypeInfoPB* udtype = resp->add_udtypes();
    udtype->set_id(entry.second->id());
    udtype->set_name(ltm->name());
    for (int i = 0; i <= ltm->field_names_size(); i++) {
      udtype->add_field_names(ltm->field_names(i));
    }
    for (int i = 0; i <= ltm->field_types_size(); i++) {
      udtype->add_field_types()->CopyFrom(ltm->field_types(i));
    }

    if (CHECK_NOTNULL(ns.get())) {
      auto l = ns->LockForRead();
      udtype->mutable_namespace_()->set_id(ns->id());
      udtype->mutable_namespace_()->set_name(ns->name());
    }
  }
  return Status::OK();
}

Result<uint64_t> CatalogManager::IncrementYsqlCatalogVersion() {
  return ysql_manager_->IncrementYsqlCatalogVersion(GetLeaderEpochInternal());
}

Status CatalogManager::IsInitDbDone(
    const IsInitDbDoneRequestPB* req, IsInitDbDoneResponsePB* resp) {
  auto is_operation_done = ysql_manager_->IsInitDbDone();

  if (is_operation_done.done()) {
    resp->set_done(true);
    if (!is_operation_done.status().ok()) {
      resp->set_initdb_error(is_operation_done.status().message().ToBuffer());
    }
  } else {
    resp->set_done(false);
  }

  return Status::OK();
}

Status CatalogManager::GetYsqlCatalogVersion(uint64_t* catalog_version,
                                             uint64_t* last_breaking_version) {
  return GetYsqlDBCatalogVersion(kTemplate1Oid, catalog_version, last_breaking_version);
}

Status CatalogManager::GetYsqlDBCatalogVersion(uint32_t db_oid,
                                               uint64_t* catalog_version,
                                               uint64_t* last_breaking_version) {
  auto table_id =
      VERIFY_RESULT(ysql_manager_->GetVersionSpecificCatalogTableId(kPgYbCatalogVersionTableId));
  auto table_info = GetTableInfo(table_id);
  if (table_info != nullptr) {
    RETURN_NOT_OK(sys_catalog_->ReadYsqlDBCatalogVersion(
        kPgYbCatalogVersionTableId, db_oid, catalog_version, last_breaking_version));
    // If the version is properly initialized, we're done.
    if ((!catalog_version || *catalog_version > 0) &&
        (!last_breaking_version || *last_breaking_version > 0)) {
      return Status::OK();
    }
    // However, it's possible for a table to have no entries mid-migration or if migration fails.
    // In this case we'd like to fall back to the legacy approach.
  }

  const auto version = ysql_manager_->GetYsqlCatalogVersion();
  // last_breaking_version is the last version (change) that invalidated ongoing transactions.
  // If using the old (protobuf-based) version method, we do not have any information about
  // breaking changes so assuming every change is a breaking change.
  if (catalog_version) {
    *catalog_version = version;
  }
  if (last_breaking_version) {
    *last_breaking_version = version;
  }
  return Status::OK();
}

Status CatalogManager::GetYsqlAllDBCatalogVersionsImpl(DbOidToCatalogVersionMap* versions) {
  auto table_id =
      VERIFY_RESULT(ysql_manager_->GetVersionSpecificCatalogTableId(kPgYbCatalogVersionTableId));
  auto table_info = GetTableInfo(table_id);
  if (table_info != nullptr) {
    RETURN_NOT_OK(sys_catalog_->ReadYsqlAllDBCatalogVersions(kPgYbCatalogVersionTableId, versions));
  } else {
    versions->clear();
  }
  return Status::OK();
}

// Note: versions and fingerprint are outputs.
Status CatalogManager::GetYsqlAllDBCatalogVersions(
    bool use_cache, DbOidToCatalogVersionMap* versions, uint64_t* fingerprint) {
  DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
  if (use_cache && catalog_version_table_in_perdb_mode_) {
    SharedLock lock(heartbeat_pg_catalog_versions_cache_mutex_);
    // We expect that the only caller uses this cache is the heartbeat service.
    // It is ok for heartbeat_pg_catalog_versions_cache_ to be empty: the
    // heartbeat service will simply not populate catalog versions in its
    // heartbeat response message. A tserver will not change its private
    // catalog version map when it finds no catalog versions in the heartbeat
    // response message.
    // Some unit tests check tserver private map before the background task has
    // a chance to populate the cache, read from pg_yb_catalog_version below to
    // make such unit tests happy.
    if (heartbeat_pg_catalog_versions_cache_) {
      *versions = *heartbeat_pg_catalog_versions_cache_;
      if (fingerprint) {
        *fingerprint = heartbeat_pg_catalog_versions_cache_fingerprint_;
      }
      return Status::OK();
    }
  }
  // Cannot use cached data, or the cache has never been initialized yet, read
  // from pg_yb_catalog_version table.
  RETURN_NOT_OK(GetYsqlAllDBCatalogVersionsImpl(versions));
  if (fingerprint) {
    *fingerprint = FingerprintCatalogVersions<DbOidToCatalogVersionMap>(*versions);
    VLOG_WITH_FUNC(2) << "databases: " << versions->size() << ", fingerprint: " << *fingerprint;
  }
  if (!catalog_version_table_in_perdb_mode_ &&
      versions->size() > 1 && !FLAGS_TEST_disable_set_catalog_version_table_in_perdb_mode) {
    LOG(INFO) << "set catalog_version_table_in_perdb_mode_ to true";
    catalog_version_table_in_perdb_mode_ = true;
    master_->shared_object().SetCatalogVersionTableInPerdbMode(true);
  }
  return Status::OK();
}

// When a cluster is running in per-database catalog version mode, normally
// catalog_version_table_in_perdb_mode_ is set to true as part of preparing for
// tserver heartbeat response and once set to true it is never reset back to
// false. In case a new leader master has not received any tserver heartbeat
// request, then catalog_version_table_in_perdb_mode_ still has its initial
// value false, but we cannot assume that the table pg_yb_catalog_version is
// still in global mode. That's why this function does an on-demand reading of
// pg_yb_catalog_version table to find out.
Status CatalogManager::IsCatalogVersionTableInPerdbMode(bool* perdb_mode) {
  DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
  if (!catalog_version_table_in_perdb_mode_) {
    DbOidToCatalogVersionMap versions;
    RETURN_NOT_OK(GetYsqlAllDBCatalogVersions(
        false /* use_cache */, &versions, nullptr /* fingerprint */));
    // If FLAGS_TEST_disable_set_catalog_version_table_in_perdb_mode is set, for
    // unit test purpose, we return perdb_mode properly while leaving
    // catalog_version_table_in_perdb_mode_ not set.
    if (FLAGS_TEST_disable_set_catalog_version_table_in_perdb_mode) {
      *perdb_mode = versions.size() > 1;
      return Status::OK();
    }
  }
  *perdb_mode = catalog_version_table_in_perdb_mode_;
  return Status::OK();
}

Status CatalogManager::InitializeTransactionTablesConfig(int64_t term) {
  SysTransactionTablesConfigEntryPB transaction_tables_config;
  transaction_tables_config.set_version(0);

  // Create in memory objects.
  transaction_tables_config_ = new SysConfigInfo(kTransactionTablesConfigType);

  // Prepare write.
  auto l = transaction_tables_config_->LockForWrite();
  *l.mutable_data()->pb.mutable_transaction_tables_config() = std::move(transaction_tables_config);

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->Upsert(term, transaction_tables_config_));
  l.Commit();

  return Status::OK();
}

Status CatalogManager::IncrementTransactionTablesVersion() {
  auto l = CHECK_NOTNULL(transaction_tables_config_.get())->LockForWrite();
  uint64_t new_version = l->pb.transaction_tables_config().version() + 1;
  l.mutable_data()->pb.mutable_transaction_tables_config()->set_version(new_version);

  // Write to sys_catalog and in memory.
  RETURN_NOT_OK(sys_catalog_->Upsert(leader_ready_term(), transaction_tables_config_));
  l.Commit();

  LOG(INFO) << "Set transaction tables version: " << new_version;

  return Status::OK();
}

uint64_t CatalogManager::GetTransactionTablesVersion() {
  auto l = CHECK_NOTNULL(transaction_tables_config_.get())->LockForRead();
  return l->pb.transaction_tables_config().version();
}

Status CatalogManager::WaitForTransactionTableVersionUpdateToPropagate() {
  auto ts_descriptors = master_->ts_manager()->GetAllDescriptors();
  size_t num_descriptors = ts_descriptors.size();

  CountDownLatch latch(num_descriptors);
  std::vector<Status> statuses(num_descriptors, Status::OK());

  for (size_t i = 0; i < num_descriptors; ++i) {
    auto ts_uuid = ts_descriptors[i]->permanent_uuid();
    auto callback = [&latch, &statuses, i](const Status& s) {
      statuses[i] = s;
      latch.CountDown();
    };
    auto task = std::make_shared<AsyncUpdateTransactionTablesVersion>(
        master_, AsyncTaskPool(), ts_uuid, GetTransactionTablesVersion(), callback);
    auto s = ScheduleTask(task);
    if (!s.ok()) {
      statuses[i] = s;
    }
  }

  latch.Wait();

  for (const auto& status : statuses) {
    RETURN_NOT_OK(status);
  }
  return Status::OK();
}

Status CatalogManager::RegisterTsFromRaftConfig(
    const consensus::RaftPeerPB& peer, const LeaderEpoch& epoch) {
  NodeInstancePB instance_pb;
  instance_pb.set_permanent_uuid(peer.permanent_uuid());
  instance_pb.set_instance_seqno(0);

  TSRegistrationPB registration_pb;
  auto* common = registration_pb.mutable_common();
  *common->mutable_private_rpc_addresses() = peer.last_known_private_addr();
  *common->mutable_broadcast_addresses() = peer.last_known_broadcast_addr();
  *common->mutable_cloud_info() = peer.cloud_info();

  // Todo(Rahul) : May need to be changed when we implement table level overrides.
  {
    auto l = ClusterConfig()->LockForRead();
    // If the config has no replication info, use empty string for the placement uuid, otherwise
    // calculate it from the reported peer.
    auto placement_uuid = l->pb.has_replication_info()
        ? VERIFY_RESULT(CatalogManagerUtil::GetPlacementUuidFromRaftPeer(
                            l->pb.replication_info(), peer))
        : "";
    common->set_placement_uuid(placement_uuid);
  }
  return master_->ts_manager()->RegisterFromRaftConfig(
      instance_pb, registration_pb, master_->MakeCloudInfoPB(), epoch, &master_->proxy_cache());
}

Result<tablet::TabletPeerPtr> CatalogManager::GetServingTablet(const TabletId& tablet_id) const {
  return GetServingTablet(Slice(tablet_id));
}

Result<tablet::TabletPeerPtr> CatalogManager::GetServingTablet(const Slice& tablet_id) const {
  // Note: CatalogManager has only one table, 'sys_catalog', with only
  // one tablet.

  if (PREDICT_FALSE(!IsInitialized())) {
    // Master puts up the consensus service first and then initiates catalog manager's creation
    // asynchronously. So this case is possible, but harmless. The RPC will simply be retried.
    // Previously, because we weren't checking for this condition, we would fatal down stream.
    const string& reason = "CatalogManager is not yet initialized";
    YB_LOG_EVERY_N(WARNING, 1000) << reason;
    return STATUS(ServiceUnavailable, reason);
  }

  CHECK(sys_catalog_) << "sys_catalog_ must be initialized!";

  if (master_->opts().IsShellMode()) {
    return STATUS_FORMAT(NotFound,
        "In shell mode: no tablet_id $0 exists in CatalogManager", tablet_id);
  }

  if (sys_catalog_->tablet_id() == tablet_id && sys_catalog_->tablet_peer().get() != nullptr &&
      sys_catalog_->tablet_peer()->CheckRunning().ok()) {
    return tablet_peer();
  }

  return STATUS_FORMAT(NotFound,
      "no SysTable in the RUNNING state exists with tablet_id $0 in CatalogManager", tablet_id);
}

const NodeInstancePB& CatalogManager::NodeInstance() const {
  return master_->instance_pb();
}

Status CatalogManager::GetRegistration(ServerRegistrationPB* reg) const {
  return master_->GetRegistration(reg, server::RpcOnly::kTrue);
}

Status CatalogManager::UpdateMastersListInMemoryAndDisk() {
  DCHECK(master_->opts().IsShellMode());

  if (!master_->opts().IsShellMode()) {
    return STATUS(IllegalState, "Cannot update master's info when process is not in shell mode");
  }

  consensus::ConsensusStatePB consensus_state;
  RETURN_NOT_OK(GetCurrentConfig(&consensus_state));

  if (!consensus_state.has_config()) {
    return STATUS(NotFound, "No Raft config found");
  }

  RETURN_NOT_OK(sys_catalog_->ConvertConfigToMasterAddresses(consensus_state.config()));
  RETURN_NOT_OK(sys_catalog_->CreateAndFlushConsensusMeta(master_->fs_manager(),
                                                          consensus_state.config(),
                                                          consensus_state.current_term()));

  return Status::OK();
}

Status CatalogManager::EnableBgTasks() {
  LockGuard lock(mutex_);
  // Initialize refresh_ysql_tablespace_info_task_. This will be used to
  // manage the background task that refreshes tablespace info. This task
  // will be started by the CatalogManagerBgTasks below.
  refresh_ysql_tablespace_info_task_.Bind(&master_->messenger()->scheduler());

  // Initialize refresh_ysql_pg_catalog_versions_task_. This will be used to
  // manage the background task that refreshes pg catalog versions. This task
  // will be started by the CatalogManagerBgTasks below.
  refresh_ysql_pg_catalog_versions_task_.Bind(&master_->messenger()->scheduler());

  background_tasks_.reset(new CatalogManagerBgTasks(master_));
  RETURN_NOT_OK_PREPEND(background_tasks_->Init(),
                        "Failed to initialize catalog manager background tasks");

  // Add bg thread to rebuild yql system partitions.
  refresh_yql_partitions_task_.Bind(&master_->messenger()->scheduler());

  RETURN_NOT_OK(background_tasks_thread_pool_->SubmitFunc(
      [this]() { RebuildYQLSystemPartitions(); }));

  xrepl_parent_tablet_deletion_task_.Bind(&master_->messenger()->scheduler());

  return Status::OK();
}

void CatalogManager::WakeBgTaskIfPendingUpdates() {
  background_tasks_->WakeIfHasPendingUpdates();
}

Status CatalogManager::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB& req) {
  const TabletId& tablet_id = req.tablet_id();
  std::unique_lock<std::mutex> l(remote_bootstrap_mtx_, std::try_to_lock);
  if (!l.owns_lock()) {
    return STATUS_SUBSTITUTE(AlreadyPresent,
        "Remote bootstrap of tablet $0 already in progress", tablet_id);
  }

  if (!master_->opts().IsShellMode()) {
    return STATUS(IllegalState, "Cannot bootstrap a master which is not in shell mode.");
  }

  LOG(INFO) << "Starting remote bootstrap: " << req.ShortDebugString();

  ServerRegistrationPB tablet_leader_peer_conn_info;
  if (!req.is_served_by_tablet_leader()) {
    *tablet_leader_peer_conn_info.mutable_broadcast_addresses() =
        req.tablet_leader_broadcast_addr();
    *tablet_leader_peer_conn_info.mutable_private_rpc_addresses() =
        req.tablet_leader_private_addr();
    *tablet_leader_peer_conn_info.mutable_cloud_info() = req.tablet_leader_cloud_info();
  }

  HostPort bootstrap_peer_addr = HostPortFromPB(DesiredHostPort(
      req.bootstrap_source_broadcast_addr(),
      req.bootstrap_source_private_addr(),
      req.bootstrap_source_cloud_info(),
      master_->MakeCloudInfoPB()));

  RETURN_NOT_OK(master_->InitAutoFlagsFromMasterLeader(bootstrap_peer_addr));

  const string& bootstrap_peer_uuid = req.bootstrap_source_peer_uuid();
  int64_t leader_term = req.caller_term();

  std::shared_ptr<TabletPeer> old_tablet_peer;
  RaftGroupMetadataPtr meta;
  bool replacing_tablet = false;

  if (tablet_exists_) {
    old_tablet_peer = tablet_peer();
    // Nothing to recover if the remote bootstrap client start failed the last time.
    if (old_tablet_peer) {
      meta = old_tablet_peer->tablet_metadata();
      replacing_tablet = true;
    }
  }

  if (replacing_tablet) {
    // Make sure the existing tablet peer is shut down and tombstoned.
    RETURN_NOT_OK(tserver::HandleReplacingStaleTablet(meta,
                                                      old_tablet_peer,
                                                      tablet_id,
                                                      master_->fs_manager()->uuid(),
                                                      leader_term));
  }

  LOG_WITH_PREFIX(INFO) << " Initiating remote bootstrap from peer " << bootstrap_peer_uuid
            << " (" << bootstrap_peer_addr.ToString() << ").";

  auto rb_client = std::make_unique<tserver::RemoteBootstrapClient>(
      tablet_id, master_->fs_manager());

  // Download and persist the remote superblock in TABLET_DATA_COPYING state.
  if (replacing_tablet) {
    RETURN_NOT_OK(rb_client->SetTabletToReplace(meta, leader_term));
  }
  RETURN_NOT_OK(rb_client->Start(
      bootstrap_peer_uuid,
      &master_->proxy_cache(),
      bootstrap_peer_addr,
      tablet_leader_peer_conn_info,
      &meta));
  // This SetupTabletPeer is needed by rb_client to perform the remote bootstrap/fetch.
  // And the SetupTablet below to perform "local bootstrap" cannot be done until the remote fetch
  // has succeeded. So keeping them seperate for now.
  sys_catalog_->SetupTabletPeer(meta);
  if (PREDICT_FALSE(FLAGS_TEST_inject_latency_during_remote_bootstrap_secs)) {
    LOG(INFO) << "Injecting " << FLAGS_TEST_inject_latency_during_remote_bootstrap_secs
              << " seconds of latency for test";
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_inject_latency_during_remote_bootstrap_secs));
  }

  // From this point onward, the superblock is persisted in TABLET_DATA_COPYING
  // state, and we need to tombstone the tablet if additional steps prior to
  // getting to a TABLET_DATA_READY state fail.
  tablet_exists_ = true;

  // Download all of the remote files.
  TOMBSTONE_NOT_OK(rb_client->FetchAll(tablet_peer()->status_listener()),
                   meta,
                   master_->fs_manager()->uuid(),
                   Substitute("Remote bootstrap: Unable to fetch data from remote peer $0 ($1)",
                              bootstrap_peer_uuid, bootstrap_peer_addr.ToString()),
                   nullptr);

  // Write out the last files to make the new replica visible and update the
  // TabletDataState in the superblock to TABLET_DATA_READY.
  // Finish() will call EndRemoteSession() and wait for the leader to successfully submit a
  // ChangeConfig request (to change this master's role from PRE_VOTER or PRE_OBSERVER to VOTER or
  // OBSERVER respectively). If the RPC times out, we will ignore the error (since the leader could
  // have successfully submitted the ChangeConfig request and failed to respond before in time)
  // and check the committed config until we find that this master's role has changed, or until we
  // time out which will cause us to tombstone the tablet.
  TOMBSTONE_NOT_OK(rb_client->Finish(),
                   meta,
                   master_->fs_manager()->uuid(),
                   "Remote bootstrap: Failed calling Finish()",
                   nullptr);

  // Synchronous tablet open for "local bootstrap".
  RETURN_NOT_OK(tserver::ShutdownAndTombstoneTabletPeerNotOk(
      sys_catalog_->OpenTablet(meta), sys_catalog_->tablet_peer(), meta,
      master_->fs_manager()->uuid(), "Remote bootstrap: Failed opening sys catalog"));

  // Set up the in-memory master list and also flush the cmeta.
  RETURN_NOT_OK(UpdateMastersListInMemoryAndDisk());

  master_->SetShellMode(false);

  // Call VerifyChangeRoleSucceeded only after we have set shell mode to false. Otherwise,
  // CatalogManager::GetTabletPeer will always return an error, and the consensus will never get
  // updated.
  auto consensus = VERIFY_RESULT(sys_catalog_->tablet_peer()->GetConsensus());
  auto status = rb_client->VerifyChangeRoleSucceeded(consensus);

  if (!status.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Remote bootstrap finished. "
                             << "Failed calling VerifyChangeRoleSucceeded: "
                             << status.ToString();
  } else {
    LOG_WITH_PREFIX(INFO) << "Remote bootstrap finished successfully";
  }

  LOG(INFO) << "Master completed remote bootstrap and is out of shell mode.";

  // Now that we're no longer in shell mode, make call to fetch encryption keys from other masters.
  RETURN_NOT_OK(GetUniverseKeyRegistryFromOtherMastersAsync());
  RETURN_NOT_OK(EnableBgTasks());

  return Status::OK();
}

Status CatalogManager::SendAlterTableRequest(
    const scoped_refptr<TableInfo>& table, const LeaderEpoch& epoch,
    const AlterTableRequestPB* req) {
  bool is_ysql_table_with_transaction_metadata =
      table->GetTableType() == TableType::PGSQL_TABLE_TYPE &&
      req != nullptr &&
      req->has_transaction() &&
      req->transaction().has_transaction_id();

  TransactionId txn_id = TransactionId::Nil();
  if (is_ysql_table_with_transaction_metadata) {
    {
      LOG(INFO) << "Persist transaction metadata into SysTableEntryPB for table ID " << table->id();
      TRACE("Locking table");
      auto l = table->LockForWrite();
      auto& tablet_data = *l.mutable_data();
      auto& table_pb = tablet_data.pb;
      table_pb.mutable_transaction()->CopyFrom(req->transaction());

      // Update sys-catalog with the transaction ID.
      TRACE("Updating table metadata on disk");
      RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table.get()));

      // Update the in-memory state.
      TRACE("Committing in-memory state");
      l.Commit();
    }
    txn_id = VERIFY_RESULT(FullyDecodeTransactionId(req->transaction().transaction_id()));
  }

  return SendAlterTableRequestInternal(table, txn_id, epoch, req);
}

Status CatalogManager::SendAlterTableRequestInternal(
    const scoped_refptr<TableInfo>& table, const TransactionId& txn_id, const LeaderEpoch& epoch,
    const AlterTableRequestPB* req) {
  for (const auto& tablet : VERIFY_RESULT(table->GetTablets())) {
     std::shared_ptr<AsyncAlterTable> call;

    // CDC SDK Create Stream context
    if (req && req->has_cdc_sdk_stream_id()) {
      LOG(INFO) << " CDC stream id context : " << req->cdc_sdk_stream_id();
      xrepl::StreamId stream_id =
        VERIFY_RESULT(xrepl::StreamId::FromString(req->cdc_sdk_stream_id()));
      call = std::make_shared<AsyncAlterTable>(master_, AsyncTaskPool(), tablet, table,
                                               txn_id, epoch,
                                               stream_id,
                                               req->cdc_sdk_require_history_cutoff());
    } else {
      call = std::make_shared<AsyncAlterTable>(master_, AsyncTaskPool(), tablet, table,
                                               txn_id, epoch);
    }
    table->AddTask(call);
    if (PREDICT_FALSE(FLAGS_TEST_slowdown_alter_table_rpcs_ms > 0)) {
      LOG(INFO) << "Sleeping for " << tablet->id() << " " << FLAGS_TEST_slowdown_alter_table_rpcs_ms
                << "ms before sending async alter table request";
      SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_slowdown_alter_table_rpcs_ms));
    }
    RETURN_NOT_OK(ScheduleTask(call));
  }
  return Status::OK();
}

Status CatalogManager::SendSplitTabletRequest(
    const TabletInfoPtr& tablet, std::array<TabletId, kNumSplitParts> new_tablet_ids,
    const std::string& split_encoded_key, const std::string& split_partition_key,
    const LeaderEpoch& epoch) {
  VLOG(2) << "Scheduling SplitTablet request to leader tserver for source tablet ID: "
          << tablet->tablet_id() << ", after-split tablet IDs: " << AsString(new_tablet_ids);
  auto call = std::make_shared<AsyncSplitTablet>(
      master_, AsyncTaskPool(), tablet, new_tablet_ids, split_encoded_key, split_partition_key,
      epoch);
  tablet->table()->AddTask(call);
  return ScheduleTask(call);
}

void CatalogManager::DeleteTabletReplicas(
    const TabletInfoPtr& tablet, const std::string& msg, HideOnly hide_only, KeepData keep_data,
    const LeaderEpoch& epoch) {
  auto locations = tablet->GetReplicaLocations();
  LOG(INFO) << "Sending DeleteTablet for " << locations->size()
            << " replicas of tablet " << tablet->tablet_id();
  for (const auto& [ts_uuid, _] : *locations) {
    SendDeleteTabletRequest(tablet->tablet_id(), TABLET_DATA_DELETED, boost::none, tablet->table(),
                            ts_uuid, msg, epoch, hide_only, keep_data);
  }
}

Status CatalogManager::CheckIfForbiddenToDeleteTabletOf(const scoped_refptr<TableInfo>& table) {
  // Do not delete the system catalog tablet.
  if (table->is_system()) {
    return STATUS(InvalidArgument, "It is not allowed to delete the system table tablet");
  }
  // Do not delete the tablet of a colocated table.
  if (table->IsColocatedUserTable()) {
    return STATUS(InvalidArgument, "It is not allowed to delete tablets of the colocated tables.");
  }
  return Status::OK();
}

Status CatalogManager::DeleteOrHideTabletsOfTable(
    const TableInfoPtr& table_info, const TabletDeleteRetainerInfo& delete_retainer,
    const LeaderEpoch& epoch) {
  // Silently fail if tablet deletion is forbidden so table deletion can continue executing.
  if (!CheckIfForbiddenToDeleteTabletOf(table_info).ok()) {
    return Status::OK();
  }

  auto tablets = VERIFY_RESULT(table_info->GetTablets(IncludeInactive::kTrue));

  std::sort(tablets.begin(), tablets.end(), [](const auto& lhs, const auto& rhs) {
    return lhs->tablet_id() < rhs->tablet_id();
  });

  string deletion_msg = "Table deleted at " + LocalTimeAsString();

  RETURN_NOT_OK(DeleteOrHideTabletsAndSendRequests(tablets, delete_retainer, deletion_msg, epoch));

  if (table_info->IsColocatedDbParentTable()) {
    LockGuard lock(mutex_);
    colocated_db_tablets_map_.erase(table_info->namespace_id());
  } else if (table_info->IsTablegroupParentTable()) {
    // In the case of dropped tablegroup parent table, need to delete tablegroup info.
    LockGuard lock(mutex_);
    TablegroupId tablegroup_id = GetTablegroupIdFromParentTableId(table_info->id());
    RETURN_NOT_OK(tablegroup_manager_->Remove(tablegroup_id));
  }
  return Status::OK();
}

Status CatalogManager::DeleteOrHideTabletsAndSendRequests(
    const TabletInfos& tablets, const TabletDeleteRetainerInfo& delete_retainer,
    const std::string& reason, const LeaderEpoch& epoch) {
  const auto hide_only = HideOnly(delete_retainer.IsHideOnly());

  struct TabletData {
    TabletInfoPtr tablet;
    TabletInfo::WriteLock lock;
    bool transaction_status_tablet;
  };

  TEST_SYNC_POINT("CatalogManager::DeleteOrHideTabletsAndSendRequests::Start");

  std::vector<TabletInfoPtr> marked_as_hidden;

  std::vector<TabletData> tablets_data;
  tablets_data.reserve(tablets.size());
  std::vector<TabletInfo*> tablet_infos;
  tablet_infos.reserve(tablets_data.size());

  // Grab tablets and tablet write locks. The list should already be in tablet_id sorted order.
  for (const auto& tablet : tablets) {
    auto tablet_data = TabletData{
        .tablet = tablet,
        .lock = tablet->LockForWrite(),
        .transaction_status_tablet =
            (tablet->table()->GetTableType() == TRANSACTION_STATUS_TABLE_TYPE),
    };

    tablets_data.emplace_back(std::move(tablet_data));
    tablet_infos.emplace_back(tablet.get());
  }

  // Use the same hybrid time for all hidden tablets.
  HybridTime hide_hybrid_time = master_->clock()->Now();

  // Mark the tablets as deleted.
  for (auto& tablet_data : tablets_data) {
    auto& tablet = tablet_data.tablet;
    auto& tablet_lock = tablet_data.lock;
    if (hide_only) {
      // Don't call table()->RemoveTablet for hidden tablets as they are needed to support
      // CLONE, PITR, SELECT AS-OF.
      LOG(INFO) << "Hiding tablet" << tablet->tablet_id() << " with hide time:" << hide_hybrid_time;
      if (!tablet_lock->ListedAsHidden()) {
        marked_as_hidden.push_back(tablet);
      }

      MarkTabletAsHidden(tablet_lock.mutable_data()->pb, hide_hybrid_time, delete_retainer);
    } else {
      LOG(INFO) << "Deleting tablet " << tablet->tablet_id();
      VERIFY_RESULT(tablet->table()->RemoveTablet(tablet->id(), DeactivateOnly::kTrue));
      if (!tablet_data.transaction_status_tablet) {
        tablet_lock.mutable_data()->set_state(SysTabletsEntryPB::DELETED, reason);
      }
    }
  }

  // Update all the tablet states in raft in bulk.
  RETURN_NOT_OK(sys_catalog_->Upsert(epoch, tablet_infos));

  TEST_SYNC_POINT("CatalogManager::DeleteOrHideTabletsAndSendRequests::AddToMaps");
  RecordHiddenTablets(marked_as_hidden, delete_retainer);

  for (auto& tablet_data : tablets_data) {
    LOG(INFO) << (hide_only ? "Hid" : "Deleted") << " tablet " << tablet_data.tablet->tablet_id();
    tablet_data.lock.Commit();
  }

  for (auto& tablet_data : tablets_data) {
    auto& tablet = tablet_data.tablet;
    if (tablet_data.transaction_status_tablet) {
      // Transaction status table tablets need to be prepared before deletion.
      auto leader = VERIFY_RESULT(tablet->GetLeader());
      RETURN_NOT_OK(SendPrepareDeleteTransactionTabletRequest(
          tablet, leader->permanent_uuid(), reason, hide_only, epoch));
    } else {
      DeleteTabletReplicas(tablet, reason, hide_only, KeepData::kFalse, epoch);
    }
  }

  return Status::OK();
}

Status CatalogManager::SendPrepareDeleteTransactionTabletRequest(
    const TabletInfoPtr& tablet, const std::string& leader_uuid,
    const std::string& reason, HideOnly hide_only, const LeaderEpoch& epoch) {
  if (PREDICT_FALSE(GetAtomicFlag(&FLAGS_TEST_disable_tablet_deletion))) {
    return Status::OK();
  }
  auto table = tablet->table();
  LOG_WITH_PREFIX(INFO) << "Preparing tablet " << tablet->tablet_id() << " for deletion on leader "
                        << leader_uuid << " (" << reason << ")";
  auto call = std::make_shared<AsyncPrepareDeleteTransactionTablet>(
      master_, AsyncTaskPool(), leader_uuid, table, tablet, reason, hide_only, epoch);
  if (table) {
    table->AddTask(call);
  }

  return ScheduleTask(call);
}

void CatalogManager::SendDeleteTabletRequest(
    const TabletId& tablet_id,
    TabletDataState delete_type,
    const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
    const scoped_refptr<TableInfo>& table,
    const std::string& ts_uuid,
    const string& reason,
    const LeaderEpoch& epoch,
    HideOnly hide_only,
    KeepData keep_data) {
  if (PREDICT_FALSE(GetAtomicFlag(&FLAGS_TEST_disable_tablet_deletion))) {
    return;
  }
  LOG_WITH_PREFIX(INFO)
      << (hide_only ? "Hiding" : "Deleting") << " tablet " << tablet_id << " on peer "
      << ts_uuid << " with delete type "
      << TabletDataState_Name(delete_type) << " (" << reason << ")";
  auto call = std::make_shared<AsyncDeleteReplica>(
      master_, AsyncTaskPool(), ts_uuid, table, tablet_id, delete_type,
      cas_config_opid_index_less_or_equal, epoch,
      GetDeleteReplicaTaskThrottler(ts_uuid), reason);
  if (hide_only) {
    call->set_hide_only(hide_only);
  }
  if (keep_data) {
    call->set_keep_data(keep_data);
  }
  if (table != nullptr) {
    table->AddTask(call);
  }

  auto status = ScheduleTask(call);
  WARN_NOT_OK(status, Substitute("Failed to send delete request for tablet $0", tablet_id));
}

std::shared_ptr<AsyncDeleteReplica> CatalogManager::MakeDeleteReplicaTask(
    const TabletServerId& peer_uuid, const TableInfoPtr& table, const TabletId& tablet_id,
    tablet::TabletDataState delete_type,
    boost::optional<int64_t> cas_config_opid_index_less_or_equal, LeaderEpoch epoch,
    const std::string& reason) {
  return std::make_shared<AsyncDeleteReplica>(
      master_, AsyncTaskPool(), peer_uuid, table, tablet_id, delete_type,
      cas_config_opid_index_less_or_equal, epoch, GetDeleteReplicaTaskThrottler(peer_uuid),
      reason);
}

void CatalogManager::SetTabletReplicaLocations(
    const TabletInfoPtr& tablet, const std::shared_ptr<TabletReplicaMap>& replica_locations) {
  tablet->SetReplicaLocations(replica_locations);
  tablet_locations_version_.fetch_add(1, std::memory_order_acq_rel);
}

void CatalogManager::UpdateTabletReplicaLocations(
    const TabletInfoPtr& tablet, const std::string& ts_uuid, const TabletReplica& replica) {
  tablet->UpdateReplicaLocations(ts_uuid, replica);
  tablet_locations_version_.fetch_add(1, std::memory_order_acq_rel);
}

void CatalogManager::SendLeaderStepDownRequest(
    const TabletInfoPtr& tablet, const ConsensusStatePB& cstate,
    const string& change_config_ts_uuid, bool should_remove, const LeaderEpoch& epoch,
    const string& new_leader_uuid) {
  auto task = std::make_shared<AsyncTryStepDown>(
      master_, AsyncTaskPool(), tablet, cstate, change_config_ts_uuid, should_remove, epoch,
      new_leader_uuid);
  tablet->table()->AddTask(task);
  Status status = ScheduleTask(task);
  WARN_NOT_OK(status, Substitute("Failed to send new $0 request", task->type_name()));
}

// TODO: refactor this into a joint method with the add one.
void CatalogManager::SendRemoveServerRequest(
    const TabletInfoPtr& tablet, const ConsensusStatePB& cstate,
    const string& change_config_ts_uuid, const LeaderEpoch& epoch) {
  // Check if the user wants the leader to be stepped down.
  auto task = std::make_shared<AsyncRemoveServerTask>(
      master_, AsyncTaskPool(), tablet, cstate, change_config_ts_uuid, epoch);
  tablet->table()->AddTask(task);
  WARN_NOT_OK(ScheduleTask(task), Substitute("Failed to send new $0 request", task->type_name()));
}

void CatalogManager::SendAddServerRequest(
    const TabletInfoPtr& tablet, PeerMemberType member_type,
    const ConsensusStatePB& cstate, const string& change_config_ts_uuid, const LeaderEpoch& epoch) {
  auto task = std::make_shared<AsyncAddServerTask>(
      master_, AsyncTaskPool(), tablet, member_type, cstate, change_config_ts_uuid, epoch);
  tablet->table()->AddTask(task);
  WARN_NOT_OK(
      ScheduleTask(task),
      Substitute("Failed to send AddServer of tserver $0 to tablet $1",
                 change_config_ts_uuid, tablet.get()->ToString()));
}

void CatalogManager::GetPendingServerTasksUnlocked(
    const TableId &table_uuid,
    TabletToTabletServerMap *add_replica_tasks_map,
    TabletToTabletServerMap *remove_replica_tasks_map,
    TabletToTabletServerMap *stepdown_leader_tasks_map) {

  auto table = GetTableInfoUnlocked(table_uuid);
  for (const auto& task : table->GetTasks()) {
    TabletToTabletServerMap* outputMap = nullptr;
    if (task->type() == server::MonitoredTaskType::kAddServer) {
      outputMap = add_replica_tasks_map;
    } else if (task->type() == server::MonitoredTaskType::kRemoveServer) {
      outputMap = remove_replica_tasks_map;
    } else if (task->type() == server::MonitoredTaskType::kTryStepDown) {
      // Store new_leader_uuid instead of change_config_ts_uuid.
      auto raft_task = static_cast<AsyncTryStepDown*>(task.get());
      (*stepdown_leader_tasks_map)[raft_task->tablet_id()] = raft_task->new_leader_uuid();
      continue;
    }
    if (outputMap) {
      auto raft_task = static_cast<CommonInfoForRaftTask*>(task.get());
      (*outputMap)[raft_task->tablet_id()] = raft_task->change_config_ts_uuid();
    }
  }
}

void CatalogManager::ExtractTabletsToProcess(
    TabletInfos *tablets_to_delete,
    TableToTabletInfos *tablets_to_process) {
  SharedLock lock(mutex_);

  // TODO: At the moment we loop through all the tablets
  //       we can keep a set of tablets waiting for "assignment"
  //       or just a counter to avoid to take the lock and loop through the tablets
  //       if everything is "stable".

  for (const auto& [_, tablet] : *tablet_map_) {
    auto table = tablet->table();
    if (!table) {
      // Tablet is orphaned or in preparing state, continue.
      continue;
    }

    // acquire table lock before tablets.
    auto table_lock = table->LockForRead();
    auto tablet_lock = tablet->LockForRead();

    // If the table is deleted or the tablet was replaced at table creation time.
    if (tablet_lock->is_deleted() || table_lock->started_deleting()) {
      // Process this table deletion only once (tombstones for table may remain longer).
      if (tables_->FindTableOrNull(tablet->table()->id()) != nullptr) {
        tablets_to_delete->push_back(tablet);
      }
      // Don't process deleted tables regardless.
      continue;
    }

    // Running tablets.
    if (tablet_lock->is_running()) {
      // TODO: handle last update > not responding timeout?
      continue;
    }

    // Tablets not yet assigned or with a report just received.
    (*tablets_to_process)[tablet->table()->id()].push_back(tablet);
  }
}

bool CatalogManager::AreTablesDeletingOrHiding() {
  SharedLock lock(mutex_);

  for (const auto& table : tables_->GetAllTables()) {
    auto table_lock = table->LockForRead();
    // TODO(jason): possibly change this to started_deleting when we begin removing DELETED tables
    // from tables_ (see CleanUpDeletedTables).
    if (table_lock->is_deleting() || table_lock->is_hiding()) {
      return true;
    }
  }
  return false;
}

struct DeferredAssignmentActions {
  std::vector<TabletInfoPtr> modified_tablets;
  std::vector<TabletInfoPtr> needs_create_rpc;
};

void CatalogManager::HandleAssignPreparingTablet(const TabletInfoPtr& tablet,
                                                 DeferredAssignmentActions* deferred) {
  // The tablet was just created (probably by a CreateTable RPC).
  // Update the state to "creating" to be ready for the creation request.
  tablet->mutable_metadata()->mutable_dirty()->set_state(
    SysTabletsEntryPB::CREATING, "Sending initial creation of tablet");
  deferred->modified_tablets.push_back(tablet);
  deferred->needs_create_rpc.push_back(tablet);
  VLOG(1) << "Assign new tablet " << tablet->ToString();
}

Status CatalogManager::HandleAssignCreatingTablet(const TabletInfoPtr& tablet,
                                                  DeferredAssignmentActions* deferred,
                                                  vector<TabletInfoPtr>* new_tablets) {
  MonoDelta time_since_updated =
      MonoTime::Now().GetDeltaSince(tablet->last_update_time());
  int64_t remaining_timeout_ms =
      FLAGS_tablet_creation_timeout_ms - time_since_updated.ToMilliseconds();

  if (tablet->LockForRead()->pb.has_split_parent_tablet_id()) {
    // No need to recreate post-split tablets, since this is always done on source tablet replicas.
    VLOG(2) << "Post-split tablet " << AsString(tablet) << " still being created.";
    return Status::OK();
  }

  if (tablet->LockForRead()->pb.created_by_clone()) {
    // No need to recreate cloned tablets, since this is always done on source tablet replicas.
    VLOG(2) << "Cloned tablet " << AsString(tablet) << " still being created.";
    return Status::OK();
  }

  // Skip the tablet if the assignment timeout is not yet expired.
  if (remaining_timeout_ms > 0) {
    VLOG(2) << "Tablet " << tablet->ToString() << " still being created. "
            << remaining_timeout_ms << "ms remain until timeout.";
    return Status::OK();
  }

  const PersistentTabletInfo& old_info = tablet->metadata().state();

  // The "tablet creation" was already sent, but we didn't receive an answer
  // within the timeout. So the tablet will be replaced by a new one.
  TabletInfoPtr replacement;
  {
    LockGuard lock(mutex_);
    replacement = CreateTabletInfo(
        tablet->table().get(), old_info.pb.partition());
  }
  LOG(WARNING) << "Tablet " << tablet->ToString() << " was not created within "
               << "the allowed timeout. Replacing with a new tablet "
               << replacement->tablet_id();

  RETURN_NOT_OK(tablet->table()->ReplaceTablet(tablet, replacement));
  {
    LockGuard lock(mutex_);
    auto tablet_map_checkout = tablet_map_.CheckOut();
    (*tablet_map_checkout)[replacement->tablet_id()] = replacement;
  }

  // Mark old tablet as replaced.
  tablet->mutable_metadata()->mutable_dirty()->set_state(
    SysTabletsEntryPB::REPLACED,
    Substitute("Replaced by $0 at $1",
               replacement->tablet_id(), LocalTimeAsString()));

  // Mark new tablet as being created.
  replacement->mutable_metadata()->mutable_dirty()->set_state(
    SysTabletsEntryPB::CREATING,
    Substitute("Replacement for $0", tablet->tablet_id()));

  deferred->modified_tablets.push_back(tablet);
  deferred->modified_tablets.push_back(replacement);
  deferred->needs_create_rpc.push_back(replacement);
  VLOG(1) << "Replaced tablet " << tablet->tablet_id()
          << " with " << replacement->tablet_id()
          << " (table " << tablet->table()->ToString() << ")";

  new_tablets->push_back(replacement);
  return Status::OK();
}

// TODO: we could batch the IO onto a background thread.
Status CatalogManager::HandleTabletSchemaVersionReport(
    TabletInfo* tablet, uint32_t version, const LeaderEpoch& epoch,
    const scoped_refptr<TableInfo>& table_info) {
  scoped_refptr<TableInfo> table;
  if (table_info) {
    table = table_info;
  } else {
    table = tablet->table();
  }

  // Update the schema version if it's the latest.
  tablet->set_reported_schema_version(table->id(), version);
  VLOG_WITH_PREFIX_AND_FUNC(1)
      << "Tablet " << tablet->tablet_id() << " reported version " << version;

  // Verify if it's the last tablet report, and the alter completed.
  {
    auto l = table->LockForRead();
    if (l->pb.state() != SysTablesEntryPB::ALTERING) {
      VLOG_WITH_PREFIX_AND_FUNC(2) << "Table " << table->ToString() << " is not altering";
      return Status::OK();
    }

    uint32_t current_version = l->pb.version();
    if (VERIFY_RESULT(table->IsAlterInProgress(current_version))) {
      VLOG_WITH_PREFIX_AND_FUNC(2) << "Table " << table->ToString() << " has IsAlterInProgress ("
                                   << current_version << ")";
      return Status::OK();
    }

    // If it's undergoing a PITR restore, no need to launch next version.
    if (VERIFY_RESULT(IsTableUndergoingPitrRestore(*table))) {
      LOG(INFO) << "Table " << table->ToString() << " is undergoing a PITR restore currently";
      return Status::OK();
    }
  }

  // Clean up any DDL verification state that is waiting for this Alter to complete.
  RemoveDdlTransactionState(table->id(), table->EraseDdlTxnsWaitingForSchemaVersion(version));

  return MultiStageAlterTable::LaunchNextTableInfoVersionIfNecessary(this, table, version, epoch);
}

Status CatalogManager::ProcessPendingAssignmentsPerTable(
    const TableId& table_id, const TabletInfos& tablets, const LeaderEpoch& epoch,
    CMGlobalLoadState* global_load_state) {
  VLOG(1) << "Processing pending assignments";

  TSDescriptorVector ts_descs = GetAllLiveNotBlacklistedTServers();

  // Initialize this table load state.
  CMPerTableLoadState table_load_state(global_load_state);
  RETURN_NOT_OK(InitializeTableLoadState(table_id, ts_descs, &table_load_state));
  table_load_state.SortLoad();

  // Take write locks on all tablets to be processed, and ensure that they are
  // unlocked at the end of this scope.
  for (const TabletInfoPtr& tablet : tablets) {
    tablet->mutable_metadata()->StartMutation();
  }
  ScopedInfoCommitter<TabletInfo> unlocker_in(&tablets);

  // Any tablets created by the helper functions will also be created in a
  // locked state, so we must ensure they are unlocked before we return to
  // avoid deadlocks.
  TabletInfos new_tablets;
  ScopedInfoCommitter<TabletInfo> unlocker_out(&new_tablets);

  DeferredAssignmentActions deferred;

  // Iterate over each of the tablets and handle it, whatever state
  // it may be in. The actions required for the tablet are collected
  // into 'deferred'.
  for (const TabletInfoPtr& tablet : tablets) {
    SysTabletsEntryPB::State t_state = tablet->metadata().state().pb.state();

    switch (t_state) {
      case SysTabletsEntryPB::PREPARING:
        HandleAssignPreparingTablet(tablet, &deferred);
        break;

      case SysTabletsEntryPB::CREATING:
        RETURN_NOT_OK(HandleAssignCreatingTablet(tablet, &deferred, &new_tablets));
        break;

      default:
        VLOG(2) << "Nothing to do for tablet " << tablet->tablet_id() << ": state = "
                << SysTabletsEntryPB_State_Name(t_state);
        break;
    }
  }

  // Nothing to do.
  if (deferred.modified_tablets.empty() &&
      deferred.needs_create_rpc.empty()) {
    return Status::OK();
  }

  // For those tablets which need to be created in this round, assign replicas.
  Status s;
  std::unordered_set<TableInfo*> ok_status_tables;
  for (const TabletInfoPtr& tablet : deferred.needs_create_rpc) {
    // NOTE: if we fail to select replicas on the first pass (due to
    // insufficient Tablet Servers being online), we will still try
    // again unless the tablet/table creation is cancelled.
    LOG(INFO) << "Selecting replicas for tablet " << tablet->id();
    s = SelectReplicasForTablet(ts_descs, tablet, &table_load_state, global_load_state);
    if (!s.ok()) {
      s = s.CloneAndPrepend(Substitute(
          "An error occurred while selecting replicas for tablet $0: $1",
          tablet->tablet_id(), s.ToString()));
      tablet->table()->SetCreateTableErrorStatus(s);
      break;
    } else {
      ok_status_tables.emplace(tablet->table().get());
    }
  }

  // Update the sys catalog with the new set of tablets/metadata.
  if (s.ok()) {
    // If any of the ok_status_tables had an error in the previous iterations, we
    // need to clear up the error status to reflect that all the create tablets have now
    // succeded.
    for (TableInfo* table : ok_status_tables) {
      table->SetCreateTableErrorStatus(Status::OK());
    }

    s = sys_catalog_->Upsert(epoch, deferred.modified_tablets);
    if (!s.ok()) {
      s = s.CloneAndPrepend("An error occurred while persisting the updated tablet metadata");
    }
  }

  if (!s.ok()) {
    LOG(WARNING) << "Aborting the current task due to error: " << s.ToString();
    // If there was an error, abort any mutations started by the current task.
    // NOTE: Lock order should be lock_ -> table -> tablet.
    // We currently have a bunch of tablets locked and need to unlock first to ensure this holds.

    std::sort(new_tablets.begin(), new_tablets.end(), [](const auto& lhs, const auto& rhs) {
      return lhs->table().get() < rhs->table().get();
    });
    {
      std::string current_table_name;
      TableInfoPtr current_table;
      for (auto& tablet_to_remove : new_tablets) {
        if (VERIFY_RESULT(tablet_to_remove->table()->RemoveTablet(tablet_to_remove->tablet_id()))) {
          if (VLOG_IS_ON(1)) {
            if (current_table != tablet_to_remove->table()) {
              current_table = tablet_to_remove->table();
              current_table_name = current_table->name();
            }
            LOG(INFO) << "Removed tablet " << tablet_to_remove->tablet_id() << " from table "
                      << current_table_name;
          }
        }
      }
    }

    unlocker_out.Abort();  // tablet.unlock
    unlocker_in.Abort();

    {
      LockGuard lock(mutex_); // lock_.lock
      auto tablet_map_checkout = tablet_map_.CheckOut();
      for (auto& tablet_to_remove : new_tablets) {
        // Potential race condition above, but it's okay if a background thread deleted this.
        tablet_map_checkout->erase(tablet_to_remove->tablet_id());
      }
    }
    return s;
  }

  // Send DeleteTablet requests to tablet servers serving deleted tablets.
  // This is asynchronous / non-blocking.
  for (const TabletInfoPtr& tablet : deferred.modified_tablets) {
    if (tablet->metadata().dirty().is_deleted()) {
      // Actual delete, because we delete tablet replica.
      DeleteTabletReplicas(tablet, tablet->metadata().dirty().pb.state_msg(), HideOnly::kFalse,
                           KeepData::kFalse, epoch);
    }
  }
  // Send the CreateTablet() requests to the servers. This is asynchronous / non-blocking.
  return SendCreateTabletRequests(deferred.needs_create_rpc, epoch);
}

Status CatalogManager::SelectProtegeForTablet(
    const TabletInfoPtr& tablet, consensus::RaftConfigPB* config, CMGlobalLoadState* global_state) {
  // Find the tserver with the lowest pending protege load.
  const std::string* ts_uuid = nullptr;
  uint32_t min_protege_load = std::numeric_limits<uint32_t>::max();
  for (const RaftPeerPB& peer : config->peers()) {
    const uint32_t protege_load =
        VERIFY_RESULT(global_state->GetGlobalProtegeLoad(peer.permanent_uuid()));
    if (protege_load < min_protege_load) {
      ts_uuid = &peer.permanent_uuid();
      min_protege_load = protege_load;
    }
  }

  if (ts_uuid == nullptr) {
    DCHECK_EQ(config->peers_size(), 0);
    DCHECK_EQ(min_protege_load, std::numeric_limits<uint32_t>::max());
    return STATUS(NotFound, "Unable to locate protege for tablet", tablet->tablet_id());
  }

  tablet->SetInitiaLeaderElectionProtege(*ts_uuid);

  ++global_state->per_ts_protege_load_[*ts_uuid];

  return Status::OK();
}

Status CatalogManager::SelectReplicasForTablet(
    const TSDescriptorVector& ts_descs, const TabletInfoPtr& tablet,
    CMPerTableLoadState* per_table_state, CMGlobalLoadState* global_state) {
  auto table_guard = tablet->table()->LockForRead();

  if (!table_guard->pb.IsInitialized()) {
    return STATUS_SUBSTITUTE(InvalidArgument,
        "TableInfo for tablet $0 is not initialized (aborted CreateTable attempt?)",
        tablet->tablet_id());
  }

  const auto& replication_info =
    VERIFY_RESULT(GetTableReplicationInfo(table_guard->pb.replication_info(),
          tablet->table()->TablespaceIdForTableCreation()));

  ConsensusStatePB* cstate = tablet->mutable_metadata()->mutable_dirty()
          ->pb.mutable_committed_consensus_state();
  VLOG_WITH_FUNC(3) << "Committed consensus state: " << AsString(cstate);
  consensus::RaftConfigPB* config = cstate->mutable_config();

  RETURN_NOT_OK(HandlePlacementUsingReplicationInfo(
      replication_info, ts_descs, config, per_table_state, global_state));

  std::ostringstream out;
  out << "Initial tserver uuids for tablet " << tablet->tablet_id() << ": ";
  for (const RaftPeerPB& peer : config->peers()) {
    out << peer.permanent_uuid() << " ";
  }
  VLOG(0) << out.str();

  // Select a protege for the initial leader election. The selected protege is stored in 'tablet'
  // but is not used until the master starts the initial leader election. The master will start the
  // initial leader after it receives a quorum of reports from bootstrapped replicas.
  RETURN_NOT_OK(SelectProtegeForTablet(tablet, config, global_state));

  LOG(INFO) << "Initial tserver protege for tablet " << tablet->tablet_id() << ": "
            << tablet->InitiaLeaderElectionProtege();

  VLOG_WITH_FUNC(3) << "Committed consensus state has been updated to: " << AsString(cstate);

  return Status::OK();
}

void CatalogManager::GetTsDescsFromPlacementInfo(const PlacementInfoPB& placement_info,
                                                 const TSDescriptorVector& all_ts_descs,
                                                 TSDescriptorVector* ts_descs) {
  ts_descs->clear();
  for (const auto& ts_desc : all_ts_descs) {
    if (placement_info.has_placement_uuid()) {
      string placement_uuid = placement_info.placement_uuid();
      if (ts_desc->placement_uuid() == placement_uuid) {
        ts_descs->push_back(ts_desc);
      }
    } else if (ts_desc->placement_uuid() == "") {
      // Since the placement info has no placement id, we know it is live, so we add this ts.
      ts_descs->push_back(ts_desc);
    }
  }
}

Status CatalogManager::HandlePlacementUsingReplicationInfo(
    const ReplicationInfoPB& replication_info,
    const TSDescriptorVector& all_ts_descs,
    consensus::RaftConfigPB* config,
    CMPerTableLoadState* per_table_state,
    CMGlobalLoadState* global_state) {
  // Validate if we have enough tservers to put the replicas.
  ValidateReplicationInfoRequestPB req;
  req.mutable_replication_info()->CopyFrom(replication_info);
  ValidateReplicationInfoResponsePB resp;
  RETURN_NOT_OK(ValidateReplicationInfo(&req, &resp));

  TSDescriptorVector ts_descs;
  GetTsDescsFromPlacementInfo(replication_info.live_replicas(), all_ts_descs, &ts_descs);
  RETURN_NOT_OK(HandlePlacementUsingPlacementInfo(
      replication_info.live_replicas(), ts_descs, PeerMemberType::VOTER,
      config, per_table_state, global_state));
  for (int i = 0; i < replication_info.read_replicas_size(); i++) {
    GetTsDescsFromPlacementInfo(replication_info.read_replicas(i), all_ts_descs, &ts_descs);
    RETURN_NOT_OK(HandlePlacementUsingPlacementInfo(
        replication_info.read_replicas(i), ts_descs, PeerMemberType::OBSERVER,
        config, per_table_state, global_state));
  }
  return Status::OK();
}

Status CatalogManager::HandlePlacementUsingPlacementInfo(const PlacementInfoPB& placement_info,
                                                         const TSDescriptorVector& ts_descs,
                                                         PeerMemberType member_type,
                                                         consensus::RaftConfigPB* config,
                                                         CMPerTableLoadState* per_table_state,
                                                         CMGlobalLoadState* global_state) {
  size_t nreplicas = GetNumReplicasOrGlobalReplicationFactor(placement_info);
  size_t ntservers = ts_descs.size();
  // Keep track of servers we've already selected, so that we don't attempt to
  // put two replicas on the same host.
  set<TabletServerId> already_selected_ts;
  if (placement_info.placement_blocks().empty()) {
    // If we don't have placement info, just place the replicas as before, distributed across the
    // whole cluster.
    // We cannot put more than ntservers replicas.
    nreplicas = min(nreplicas, ntservers);
    SelectReplicas(ts_descs, nreplicas, config, &already_selected_ts, member_type,
                   per_table_state, global_state);
  } else {
    // TODO(bogdan): move to separate function
    //
    // If we do have placement info, we'll try to use the same power of two algorithm, but also
    // match the requested policies. We'll assign the minimum requested replicas in each combination
    // of cloud.region.zone and then if we still have leftover replicas, we'll assign those
    // in any of the allowed areas.
    auto all_allowed_ts = VERIFY_RESULT(FindTServersForPlacementInfo(placement_info, ts_descs));

    // Loop through placements and assign to respective available TSs.
    size_t min_replica_count_sum = 0;
    for (const auto& pb : placement_info.placement_blocks()) {
      // This works because currently we don't allow placement blocks to overlap.
      auto available_ts_descs = VERIFY_RESULT(FindTServersForPlacementBlock(pb, ts_descs));
      size_t available_ts_descs_size = available_ts_descs.size();
      size_t min_num_replicas = pb.min_num_replicas();
      // We cannot put more than the available tablet servers in that placement block.
      size_t num_replicas = min(min_num_replicas, available_ts_descs_size);
      min_replica_count_sum += min_num_replicas;
      SelectReplicas(available_ts_descs, num_replicas, config, &already_selected_ts, member_type,
                     per_table_state, global_state);
    }

    size_t replicas_left = nreplicas - min_replica_count_sum;
    size_t max_tservers_left = all_allowed_ts.size() - already_selected_ts.size();
    // Upper bounded by the tservers left.
    replicas_left = min(replicas_left, max_tservers_left);
    DCHECK_GE(replicas_left, 0);
    if (replicas_left > 0) {
      // No need to do an extra check here, as we checked early if we have enough to cover all
      // requested placements and checked individually per placement info, if we could cover the
      // minimums.
      SelectReplicas(all_allowed_ts, replicas_left, config, &already_selected_ts, member_type,
                     per_table_state, global_state);
    }
  }
  return Status::OK();
}

Result<vector<shared_ptr<TSDescriptor>>> CatalogManager::FindTServersForPlacementInfo(
    const PlacementInfoPB& placement_info,
    const TSDescriptorVector& ts_descs) const {

  vector<shared_ptr<TSDescriptor>> all_allowed_ts;
  for (const auto& ts : ts_descs) {
    for (const auto& pb : placement_info.placement_blocks()) {
      if (ts->MatchesCloudInfo(pb.cloud_info())) {
        all_allowed_ts.push_back(ts);
        break;
      }
    }
  }

  return all_allowed_ts;
}

Result<vector<shared_ptr<TSDescriptor>>> CatalogManager::FindTServersForPlacementBlock(
    const PlacementBlockPB& placement_block,
    const TSDescriptorVector& ts_descs) {

  vector<shared_ptr<TSDescriptor>> allowed_ts;
  const auto& cloud_info = placement_block.cloud_info();
  for (const auto& ts : ts_descs) {
    if (ts->MatchesCloudInfo(cloud_info)) {
      allowed_ts.push_back(ts);
    }
  }

  return allowed_ts;
}

Status CatalogManager::SendCreateTabletRequests(
    const std::vector<TabletInfoPtr>& tablets,
    const LeaderEpoch& epoch) {
  auto schedules_to_tablets_map = VERIFY_RESULT(
      master_->snapshot_coordinator().MakeSnapshotSchedulesToObjectIdsMap(
          SysRowEntryType::TABLET));
  for (const TabletInfoPtr& tablet : tablets) {
    const consensus::RaftConfigPB& config =
        tablet->metadata().dirty().pb.committed_consensus_state().config();
    tablet->set_last_update_time(MonoTime::Now());
    std::vector<SnapshotScheduleId> schedules;
    for (const auto& pair : schedules_to_tablets_map) {
      if (std::binary_search(pair.second.begin(), pair.second.end(), tablet->id())) {
        schedules.push_back(pair.first);
      }
    }

    bool stream_exists_on_namespace = false;
    auto namespace_id = tablet->table()->namespace_id();
    {
      SharedLock lock(mutex_);
      for (const auto& entry : cdc_stream_map_) {
        const auto stream = entry.second;
        // Set the CDCSDK retention barriers on the tablets at the time of creation only if atleast
        // one stream with replication slot consumption exists on the namespace.
        if (stream->IsCDCSDKStream() && stream->namespace_id() == namespace_id &&
            !stream->GetCdcsdkYsqlReplicationSlotName().empty()) {
          stream_exists_on_namespace =  true;
          break;
        }
      }
    }

    for (const RaftPeerPB& peer : config.peers()) {
      CDCSDKSetRetentionBarriers cdc_sdk_set_retention_barriers(
          stream_exists_on_namespace && FLAGS_ysql_yb_enable_replication_slot_consumption);
      auto task = std::make_shared<AsyncCreateReplica>(
          master_, AsyncTaskPool(), peer.permanent_uuid(), tablet, schedules, epoch,
          cdc_sdk_set_retention_barriers);
      tablet->table()->AddTask(task);
      WARN_NOT_OK(ScheduleTask(task), "Failed to send new tablet request");
    }
  }
  return Status::OK();
}

// If responses have been received from sufficient replicas (including hinted leader),
// pick proposed leader and start election.
void CatalogManager::StartElectionIfReady(
    const consensus::ConsensusStatePB& cstate, const LeaderEpoch& epoch,
    const TabletInfoPtr& tablet) {
  auto replicas = tablet->GetReplicaLocations();
  bool initial_election = cstate.current_term() == kMinimumTerm;
  int num_voters = 0;
  for (const auto& peer : cstate.config().peers()) {
    if (peer.member_type() == PeerMemberType::VOTER) {
      ++num_voters;
    }
  }
  int majority_size = num_voters / 2 + 1;
  int running_voters = 0;
  for (const auto& replica : *replicas) {
    if (replica.second.member_type == PeerMemberType::VOTER &&
        replica.second.state == RaftGroupStatePB::RUNNING) {
      ++running_voters;
    }
  }

  VLOG_WITH_PREFIX(4)
      << __func__ << ": T " << tablet->tablet_id() << ": " << AsString(*replicas) << ", voters: "
      << running_voters << "/" << majority_size;

  if (running_voters < majority_size) {
    VLOG_WITH_PREFIX(4) << __func__ << ": Not enough voters";
    return;
  }

  ReplicationInfoPB replication_info;
  {
    auto l = ClusterConfig()->LockForRead();
    replication_info = l->pb.replication_info();
  }

  // Find tservers that can be leaders for a tablet.
  TSDescriptorVector ts_descs = GetAllLiveNotBlacklistedTServers();

  std::vector<std::string> possible_leaders;
  for (const auto& replica : *replicas) {
    // Start hinted election only on running replicas if it's not initial election (create table).
    if (!initial_election && (replica.second.member_type != PeerMemberType::VOTER ||
        replica.second.state != RaftGroupStatePB::RUNNING)) {
      continue;
    }
    for (const auto& ts_desc : ts_descs) {
      if (ts_desc->permanent_uuid() == replica.first) {
        if (ts_desc->IsAcceptingLeaderLoad(replication_info)) {
          possible_leaders.push_back(replica.first);
        }
        break;
      }
    }
  }

  if (FLAGS_TEST_create_table_leader_hint_min_lexicographic) {
    std::string min_lexicographic;
    for (const auto& peer : cstate.config().peers()) {
      if (peer.member_type() == PeerMemberType::VOTER) {
        if (min_lexicographic.empty() || peer.permanent_uuid() < min_lexicographic) {
          min_lexicographic = peer.permanent_uuid();
        }
      }
    }
    if (min_lexicographic.empty() || !replicas->count(min_lexicographic)) {
      LOG_WITH_PREFIX(INFO)
          << __func__ << ": Min lexicographic is not yet ready: " << min_lexicographic;
      return;
    }
    possible_leaders = { min_lexicographic };
  }

  if (possible_leaders.empty()) {
    VLOG_WITH_PREFIX(4) << __func__ << ": Cannot pick candidate";
    return;
  }

  TEST_PAUSE_IF_FLAG(TEST_pause_before_send_hinted_election);

  if (!tablet->InitiateElection()) {
    VLOG_WITH_PREFIX(4) << __func__ << ": Already initiated";
    return;
  }

  // The table creation path may store a protege hint in the tablet info.
  std::string protege = tablet->InitiaLeaderElectionProtege();

  // The 'protege' may not exist in 'possible_leaders'. In this scenario, select a random protege
  // from the list of possible leaders. This can happen when:
  //   1) The 'protege' is unconfigured (e.g. unset/empty).
  //   2) The 'protege' corresponds to a tserver that is no longer serving a replica.
  //   3) The 'protege' corresponds to a tserver that is not accepting leader load.
  if (std::count(possible_leaders.begin(), possible_leaders.end(), protege) == 0) {
    protege = RandomElement(possible_leaders);
  }

  LOG_WITH_PREFIX(INFO)
      << "Starting election at " << tablet->tablet_id() << " in favor of " << protege;

  auto task = std::make_shared<AsyncStartElection>(
      master_, AsyncTaskPool(), protege, tablet, initial_election, epoch);
  tablet->table()->AddTask(task);
  WARN_NOT_OK(task->Run(), "Failed to send new tablet start election request");
}

shared_ptr<TSDescriptor> CatalogManager::SelectReplica(
    const TSDescriptorVector& ts_descs,
    set<TabletServerId>* excluded,
    CMPerTableLoadState* per_table_state, CMGlobalLoadState* global_state) {
  shared_ptr<TSDescriptor> found_ts;
  for (const auto& sorted_load : per_table_state->sorted_replica_load_) {
    // Don't consider a tserver that has already been considered for this tablet.
    if (excluded->count(sorted_load)) {
      continue;
    }
    // Only choose from the set of allowed tservers for this tablet.
    auto it = std::find_if(ts_descs.begin(), ts_descs.end(), [&sorted_load](const auto& ts) {
      return ts->permanent_uuid() == sorted_load;
    });

    if (it != ts_descs.end()) {
      found_ts = *it;
      break;
    }
  }

  return found_ts;
}

void CatalogManager::SelectReplicas(
    const TSDescriptorVector& ts_descs, size_t nreplicas, consensus::RaftConfigPB* config,
    set<TabletServerId>* already_selected_ts, PeerMemberType member_type,
    CMPerTableLoadState* per_table_state, CMGlobalLoadState* global_state) {
  DCHECK_LE(nreplicas, ts_descs.size());

  for (size_t i = 0; i < nreplicas; ++i) {
    shared_ptr<TSDescriptor> ts = SelectReplica(
        ts_descs, already_selected_ts, per_table_state, global_state);
    InsertOrDie(already_selected_ts, ts->permanent_uuid());
    // Update the load state at global and table level.
    per_table_state->per_ts_replica_load_[ts->permanent_uuid()]++;
    global_state->per_ts_replica_load_[ts->permanent_uuid()]++;
    per_table_state->SortLoad();

    // Increment the number of pending replicas so that we take this selection into
    // account when assigning replicas for other tablets of the same table. This
    // value decays back to 0 over time.
    ts->IncrementRecentReplicaCreations();

    auto reg = ts->GetRegistration();

    RaftPeerPB *peer = config->add_peers();
    peer->set_permanent_uuid(ts->permanent_uuid());

    // TODO: This is temporary, we will use only UUIDs.
    TakeRegistration(&reg, peer);
    peer->set_member_type(member_type);
  }
}

Status CatalogManager::ConsensusStateToTabletLocations(const consensus::ConsensusStatePB& cstate,
                                                       TabletLocationsPB* locs_pb) {
  for (const consensus::RaftPeerPB& peer : cstate.config().peers()) {
    TabletLocationsPB_ReplicaPB* replica_pb = locs_pb->add_replicas();
    if (!peer.has_permanent_uuid()) {
      return STATUS_SUBSTITUTE(IllegalState, "Missing UUID $0", peer.ShortDebugString());
    }
    replica_pb->set_role(GetConsensusRole(peer.permanent_uuid(), cstate));
    if (peer.has_member_type()) {
      replica_pb->set_member_type(peer.member_type());
    } else {
      replica_pb->set_member_type(PeerMemberType::UNKNOWN_MEMBER_TYPE);
    }
    TSInfoPB* tsinfo_pb = replica_pb->mutable_ts_info();
    tsinfo_pb->set_permanent_uuid(peer.permanent_uuid());
    CopyRegistration(peer, tsinfo_pb);
  }
  locs_pb->set_raft_config_opid_index(cstate.config().opid_index());
  return Status::OK();
}

Status CatalogManager::BuildLocationsForSystemTablet(
    const TabletInfoPtr& tablet,
    TabletLocationsPB* locs_pb,
    PartitionsOnly partitions_only) {
  DCHECK(system_tablets_.find(tablet->id()) != system_tablets_.end())
      << Format("Non-system tablet $0 passed to BuildLocationsForSystemTablet", tablet->id());
  auto l_tablet = tablet->LockForRead();
  InitializeTabletLocationsPB(tablet->tablet_id(), l_tablet->pb, locs_pb);
  locs_pb->set_stale(false);
  if (partitions_only) {
    return Status::OK();
  }
  // TODO(zdrudi): support new table_ids schema for system tablets.
  *locs_pb->mutable_table_ids() = l_tablet->pb.table_ids();
  // For system tables, the set of replicas is always the set of masters.
  consensus::ConsensusStatePB master_consensus;
  RETURN_NOT_OK(GetCurrentConfig(&master_consensus));
  RETURN_NOT_OK(ConsensusStateToTabletLocations(master_consensus, locs_pb));
  return Status::OK();
}

Status CatalogManager::MaybeCreateLocalTransactionTable(
    const CreateTableRequestPB& request, rpc::RpcContext* rpc, const LeaderEpoch& epoch) {
  // If this is a transactional table and there is a associated tablespace, try to create a
  // local transaction status table for the tablespace if there is a placement attached to it
  // (and if it does not exist already).
  const bool is_transactional = request.schema().table_properties().is_transactional();
  if (GetAtomicFlag(&FLAGS_auto_create_local_transaction_tables)) {
    if (is_transactional && request.has_tablespace_id()) {
      const auto& tablespace_id = request.tablespace_id();
      auto tablespace_pb = VERIFY_RESULT(GetTablespaceReplicationInfoWithRetry(tablespace_id));
      if (tablespace_pb) {
        RETURN_NOT_OK(CreateLocalTransactionStatusTableIfNeeded(rpc, tablespace_id, epoch));
      } else {
        VLOG(1) << "Not attempting to create a local transaction status table: "
                << "tablespace " << YB_EXPR_TO_STREAM(tablespace_id) << " has no placement";
      }
    } else {
      VLOG(1) << "Not attempting to create a local transaction status table:\n"
              << YB_EXPR_TO_STREAM_ONE_PER_LINE(
                  kTwoSpaceIndent, is_transactional, request.has_tablespace_id());
    }
  }
  return Status::OK();
}

Result<int> CatalogManager::CalculateNumTabletsForTableCreation(
    const CreateTableRequestPB& request, const Schema& schema,
    const PlacementInfoPB& placement_info) {
  // Calculate number of tablets to be used. Priorities:
  //   1. Use Internally specified value from 'CreateTableRequestPB::num_tablets'.
  //   2. Use User specified value from
  //      'CreateTableRequestPB::SchemaPB::TablePropertiesPB::num_tablets'.
  //      Note, that the number will be saved in schema stored in the master persistent
  //      SysCatalog irrespective of which way we choose the number of tablets to create.
  //      If nothing is specified in this field, nothing will be stored in the table
  //      TablePropertiesPB for number of tablets
  //   3. Calculate own value.
  int num_tablets = 0;
  if (request.has_num_tablets()) {
    num_tablets = request.num_tablets();  // Internal request.
  }

  if (num_tablets <= 0 && schema.table_properties().HasNumTablets()) {
    num_tablets = schema.table_properties().num_tablets();  // User request.
  }

  if (num_tablets <= 0) {
    // Use default as client could have gotten the value before any tserver had heartbeated
    // to (a new) master leader.
    // TODO: should we check num_live_tservers is greater than 0 and return IllegalState if not?
    const auto num_live_tservers = GetNumLiveTServersForPlacement(placement_info.placement_uuid());
    num_tablets = GetInitialNumTabletsPerTable(request.table_type(), num_live_tservers);
    LOG(INFO) << "Setting default tablets to " << num_tablets << " with " << num_live_tservers
              << " primary servers";
  }

  if (schema.SchemaName() == xcluster::kDDLQueuePgSchemaName &&
      (request.name() == xcluster::kDDLQueueTableName ||
       request.name() == xcluster::kDDLReplicatedTableName)) {
    // xCluster DDL queue tables need to be single tablet tables - This ensures that we have a
    // singular stream of DDLs which simplifies ordering guarantees.
    num_tablets = 1;
  }
  return num_tablets;
}

Result<std::pair<PartitionSchema, std::vector<Partition>>> CatalogManager::CreatePartitions(
    const Schema& schema,
    int num_tablets,
    bool colocated,
    CreateTableRequestPB* request,
    CreateTableResponsePB* resp) {
  PartitionSchema partition_schema;
  vector<Partition> partitions;
  if (colocated) {
    RETURN_NOT_OK(partition_schema.CreatePartitions(1, &partitions));
    request->clear_partition_schema();
    num_tablets = 1;
  } else {
    RETURN_NOT_OK(PartitionSchema::FromPB(request->partition_schema(), schema, &partition_schema));
    if (request->partitions_size() > 0) {
      if (request->partitions_size() != num_tablets) {
        Status s = STATUS(InvalidArgument, "Partitions are not defined for all tablets");
        return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
      }
      string last;
      for (const auto& p : request->partitions()) {
        Partition np;
        Partition::FromPB(p, &np);
        if (np.partition_key_start() != last) {
          Status s =
              STATUS(InvalidArgument, "Partitions does not cover the full partition keyspace");
          return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_SCHEMA, s);
        }
        last = np.partition_key_end();
        partitions.push_back(std::move(np));
      }
    } else {
      // Supplied number of partitions is merely a suggestion, actual number of
      // created partitions might differ.
      RETURN_NOT_OK(partition_schema.CreatePartitions(num_tablets, &partitions));
    }
    // The vector 'partitions' contains real setup partitions, so the variable
    // should be updated.
    num_tablets = narrow_cast<int>(partitions.size());
  }
  LOG(INFO) << "Set number of tablets: " << num_tablets;
  request->set_num_tablets(num_tablets);
  return std::pair{partition_schema, partitions};
}

Status CatalogManager::BuildLocationsForTablet(
    const TabletInfoPtr& tablet,
    TabletLocationsPB* locs_pb,
    IncludeHidden include_hidden_tablets,
    PartitionsOnly partitions_only) {

  if (system_tablets_.find(tablet->id()) != system_tablets_.end()) {
    return BuildLocationsForSystemTablet(tablet, locs_pb, partitions_only);
  }
  std::shared_ptr<const TabletReplicaMap> locs;
  consensus::ConsensusStatePB cstate;
  {
    auto l_tablet = tablet->LockForRead();

    // Hidden tablet locations are needed to support xCluster, CDC, CLONE, SELECT AS-OF.
    if (l_tablet->is_hidden() && !include_hidden_tablets) {
      return STATUS_FORMAT(NotFound, "Tablet $0 hidden", tablet->id());
    }

    if (PREDICT_FALSE(l_tablet->is_deleted())) {
      std::vector<TabletId> split_tablet_ids(
          l_tablet->pb.split_tablet_ids().begin(), l_tablet->pb.split_tablet_ids().end());
      return STATUS(
          NotFound, "Tablet deleted", l_tablet->pb.state_msg(),
          SplitChildTabletIdsData(split_tablet_ids));
    }
    if (PREDICT_FALSE(!l_tablet->is_running())) {
      return STATUS_FORMAT(ServiceUnavailable, "Tablet $0 not running", tablet->id());
    }
    InitializeTabletLocationsPB(tablet->tablet_id(), l_tablet->pb, locs_pb);
    locs = tablet->GetReplicaLocations();
    locs_pb->set_stale(locs->empty());
    locs_pb->set_raft_config_opid_index(
        l_tablet->pb.committed_consensus_state().config().opid_index());
    if (partitions_only) {
      return Status::OK();
    }
    const auto& tablet_pb = l_tablet->pb;
    locs_pb->set_table_id(l_tablet->pb.table_id());
    if (l_tablet->pb.hosted_tables_mapped_by_parent_id()) {
      for (auto& table_id : tablet->GetTableIds()) {
        locs_pb->add_table_ids(std::move(table_id));
      }
    } else {
      *locs_pb->mutable_table_ids() = l_tablet->pb.table_ids();
    }
    if (locs->empty() && l_tablet->pb.has_committed_consensus_state()) {
      cstate = l_tablet->pb.committed_consensus_state();
    }
    locs_pb->mutable_split_tablet_ids()->Reserve(tablet_pb.split_tablet_ids().size());
    for (const auto& split_tablet_id : tablet_pb.split_tablet_ids()) {
      *locs_pb->add_split_tablet_ids() = split_tablet_id;
    }
  }

  // If the locations are cached.
  if (!locs->empty()) {
    if (cstate.IsInitialized() &&
        locs->size() != implicit_cast<size_t>(cstate.config().peers_size())) {
      LOG(WARNING) << "Cached tablet replicas " << locs->size() << " does not match consensus "
                   << cstate.config().peers_size();
    }

    locs_pb->mutable_replicas()->Reserve(narrow_cast<int32_t>(locs->size()));
    for (const auto& [ts_uuid, tablet_replica] : *locs) {
      TabletLocationsPB_ReplicaPB* replica_pb = locs_pb->add_replicas();
      replica_pb->set_role(tablet_replica.role);
      replica_pb->set_member_type(tablet_replica.member_type);
      replica_pb->set_state(tablet_replica.state);
      TSInfoPB* out_ts_info = replica_pb->mutable_ts_info();
      out_ts_info->set_permanent_uuid(ts_uuid);
      auto strong_ts_desc_ptr = tablet_replica.ts_desc.lock();
      if (strong_ts_desc_ptr) {
        CopyRegistration(strong_ts_desc_ptr->GetRegistration(), out_ts_info);
        out_ts_info->set_placement_uuid(strong_ts_desc_ptr->placement_uuid());
      }
    }
  } else if (cstate.IsInitialized()) {
    // If the locations were not cached.
    // TODO: Why would this ever happen? See KUDU-759.
    RETURN_NOT_OK(ConsensusStateToTabletLocations(cstate, locs_pb));
  }
  return Status::OK();
}

Result<shared_ptr<tablet::AbstractTablet>> CatalogManager::GetSystemTablet(const TabletId& id) {
  const auto iter = system_tablets_.find(id);
  if (iter == system_tablets_.end()) {
    return STATUS_SUBSTITUTE(InvalidArgument, "$0 is not a valid system tablet id", id);
  }
  return iter->second;
}

Status CatalogManager::GetTabletLocations(
    const TabletId& tablet_id, TabletLocationsPB* locs_pb, IncludeHidden include_hidden) {
  TabletInfoPtr tablet_info;
  {
    SharedLock lock(mutex_);
    if (!FindCopy(*tablet_map_, tablet_id, &tablet_info)) {
      return STATUS_SUBSTITUTE(NotFound, "Unknown tablet $0", tablet_id);
    }
  }
  Status s = GetTabletLocations(tablet_info, locs_pb, include_hidden);

  auto num_replicas = GetNumTabletReplicas(tablet_info);
  if (num_replicas.ok() && *num_replicas > 0 &&
      implicit_cast<size_t>(locs_pb->replicas().size()) != *num_replicas) {
    YB_LOG_EVERY_N_SECS(WARNING, 1)
        << "Expected replicas " << num_replicas << " but found "
        << locs_pb->replicas().size() << " for tablet " << tablet_info->id() << ": "
        << locs_pb->ShortDebugString() << THROTTLE_MSG;
  }
  return s;
}

Status CatalogManager::GetTabletLocations(
    const TabletInfoPtr& tablet_info,
    TabletLocationsPB* locs_pb,
    IncludeHidden include_hidden_tablets) {
  DCHECK_EQ(locs_pb->replicas().size(), 0);
  locs_pb->mutable_replicas()->Clear();
  return BuildLocationsForTablet(tablet_info, locs_pb, include_hidden_tablets);
}

Status CatalogManager::GetTableLocations(
    const GetTableLocationsRequestPB* req,
    GetTableLocationsResponsePB* resp) {
  VLOG(4) << "GetTableLocations: " << req->ShortDebugString();

  // If start-key is > end-key report an error instead of swap the two
  // since probably there is something wrong app-side.
  if (req->has_partition_key_start() && req->has_partition_key_end()
      && req->partition_key_start() > req->partition_key_end()) {
    return STATUS(InvalidArgument, "start partition key is greater than the end partition key");
  }

  if (req->max_returned_locations() <= 0) {
    return STATUS(InvalidArgument, "max_returned_locations must be greater than 0");
  }

  scoped_refptr<TableInfo> table = VERIFY_RESULT(FindTable(req->table()));

  if (table->IsCreateInProgress()) {
    resp->set_creating(true);
  }

  // Don't return TabletLocations for deleted tables as they may not exist.
  // However, do return the TabletLocations for hidden tables as those are
  // needed for supporting SELECT AS-OF, DB-Clone, XCluster, PITR.
  auto l = table->LockForRead();
  if (l->started_deleting()) {
      return STATUS_EC_FORMAT(
          NotFound, MasterError(MasterErrorPB::OBJECT_NOT_FOUND),
          "The object '$0.$1' does not exist", l->namespace_id(), l->name());
  }

  std::vector<TabletInfoPtr> tablets = VERIFY_RESULT(table->GetTabletsInRange(req));
  PartitionsOnly partitions_only(req->partitions_only());
  bool require_tablets_runnings = req->require_tablets_running();

  int expected_live_replicas = 0;
  int expected_read_replicas = 0;
  GetExpectedNumberOfReplicasForTable(table, &expected_live_replicas, &expected_read_replicas);

  resp->mutable_tablet_locations()->Reserve(narrow_cast<int32_t>(tablets.size()));
  for (const TabletInfoPtr& tablet : tablets) {
    TabletLocationsPB* locs_pb = resp->add_tablet_locations();
    locs_pb->set_expected_live_replicas(expected_live_replicas);
    locs_pb->set_expected_read_replicas(expected_read_replicas);
    auto status = BuildLocationsForTablet(tablet, locs_pb, IncludeHidden::kTrue, partitions_only);
    if (!status.ok()) {
      // Not running.
      if (require_tablets_runnings) {
        resp->mutable_tablet_locations()->Clear();
        return SetupError(resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND, status);
      }
      resp->mutable_tablet_locations()->RemoveLast();
    }
  }

  resp->set_table_type(l->pb.table_type());
  resp->set_partition_list_version(l->pb.partition_list_version());

  return Status::OK();
}

Status CatalogManager::GetCurrentConfig(consensus::ConsensusStatePB* cpb) const {
  auto tablet_peer = sys_catalog_->tablet_peer();
  SCHECK(tablet_peer, IllegalState, "Node $0 peer not initialized.", master_->fs_manager()->uuid());
  auto consensus = VERIFY_RESULT(tablet_peer->GetConsensus());

  *cpb = consensus->ConsensusState(CONSENSUS_CONFIG_COMMITTED);

  return Status::OK();
}

Status CatalogManager::DumpState(std::ostream* out, bool on_disk_dump) const {
  NamespaceInfoMap namespace_ids_copy;
  TableInfoByNameMap names_copy;
  std::vector<TableInfoPtr> tables;
  TabletInfoMap tablets_copy;

  // Copy the internal state so that, if the output stream blocks,
  // we don't end up holding the lock for a long time.
  {
    SharedLock lock(mutex_);
    namespace_ids_copy = namespace_ids_map_;
    for (const auto& table : tables_->GetAllTables()) {
      tables.push_back(table);
    }
    names_copy = table_names_map_;
    tablets_copy = *tablet_map_;
  }

  *out << "Dumping current state of master.\nNamespaces:\n";
  for (const NamespaceInfoMap::value_type& e : namespace_ids_copy) {
    NamespaceInfo* t = e.second.get();
    auto l = t->LockForRead();
    const NamespaceName& name = l->name();

    *out << t->id() << ":\n";
    *out << "  name: \"" << strings::CHexEscape(name) << "\"\n";
    *out << "  metadata: " << l->pb.ShortDebugString() << "\n";
  }

  *out << "Tables:\n";
  for (const auto& e : tables) {
    TableInfo* t = e.get();
    TabletInfos table_tablets;
    {
      auto l = t->LockForRead();
      const TableName& name = l->name();
      const NamespaceId& namespace_id = l->namespace_id();
      // Find namespace by its ID.
      scoped_refptr<NamespaceInfo> ns = FindPtrOrNull(namespace_ids_copy, namespace_id);

      *out << t->id() << ":\n";
      *out << "  namespace id: \"" << strings::CHexEscape(namespace_id) << "\"\n";

      if (ns != nullptr) {
        *out << "  namespace name: \"" << strings::CHexEscape(ns->name()) << "\"\n";
      }

      *out << "  name: \"" << strings::CHexEscape(name) << "\"\n";
      // Erase from the map, so later we can check that we don't have
      // any orphaned tables in the by-name map that aren't in the
      // by-id map.
      if (names_copy.erase({namespace_id, name}) != 1) {
        *out << "  [not present in by-name map]\n";
      }
      *out << "  metadata: " << l->pb.ShortDebugString() << "\n";

      *out << "  tablets:\n";
      table_tablets = VERIFY_RESULT(t->GetTablets());
    }
    for (const TabletInfoPtr& tablet : table_tablets) {
      auto l_tablet = tablet->LockForRead();
      *out << "    " << tablet->tablet_id() << ": "
           << l_tablet->pb.ShortDebugString() << "\n";

      if (tablets_copy.erase(tablet->tablet_id()) != 1) {
        *out << "  [ERROR: not present in CM tablet map!]\n";
      }
    }
  }

  if (!tablets_copy.empty()) {
    *out << "Orphaned tablets (not referenced by any table):\n";
    for (const TabletInfoMap::value_type& entry : tablets_copy) {
      const TabletInfoPtr& tablet = entry.second;
      auto l_tablet = tablet->LockForRead();
      *out << "    " << tablet->tablet_id() << ": "
           << l_tablet->pb.ShortDebugString() << "\n";
    }
  }

  if (!names_copy.empty()) {
    *out << "Orphaned tables (in by-name map, but not id map):\n";
    for (const TableInfoByNameMap::value_type& e : names_copy) {
      *out << e.second->id() << ":\n";
      *out << "  namespace id: \"" << strings::CHexEscape(e.first.first) << "\"\n";
      *out << "  name: \"" << CHexEscape(e.first.second) << "\"\n";
    }
  }

  master_->DumpMasterOptionsInfo(out);

  if (on_disk_dump) {
    consensus::ConsensusStatePB cur_consensus_state;
    // TODO: proper error handling below.
    CHECK_OK(GetCurrentConfig(&cur_consensus_state));
    *out << "Current raft config: " << cur_consensus_state.ShortDebugString() << "\n";

    auto cluster_config = ClusterConfig();
    if (cluster_config) {
      auto l = cluster_config->LockForRead();
      *out << "Cluster config: " << l->pb.ShortDebugString() << "\n";
    }
  }

  xcluster_manager_->DumpState(out, on_disk_dump);
  return Status::OK();
}

Status CatalogManager::PeerStateDump(const vector<RaftPeerPB>& peers,
                                     const DumpMasterStateRequestPB* req,
                                     DumpMasterStateResponsePB* resp) {
  std::unique_ptr<MasterClusterProxy> peer_proxy;
  Endpoint sockaddr;
  MonoTime timeout = MonoTime::Now();
  DumpMasterStateRequestPB peer_req;
  rpc::RpcController rpc;

  timeout.AddDelta(MonoDelta::FromMilliseconds(FLAGS_master_ts_rpc_timeout_ms));
  rpc.set_deadline(timeout);
  peer_req.set_on_disk(req->on_disk());
  peer_req.set_return_dump_as_string(req->return_dump_as_string());
  string dump;

  for (const RaftPeerPB& peer : peers) {
    HostPort hostport = HostPortFromPB(DesiredHostPort(peer, master_->MakeCloudInfoPB()));
    peer_proxy = std::make_unique<MasterClusterProxy>(&master_->proxy_cache(), hostport);

    DumpMasterStateResponsePB peer_resp;
    rpc.Reset();

    RETURN_NOT_OK(peer_proxy->DumpState(peer_req, &peer_resp, &rpc));

    if (peer_resp.has_error()) {
      LOG(WARNING) << "Hit err " << peer_resp.ShortDebugString() << " during peer "
        << peer.ShortDebugString() << " state dump.";
      return StatusFromPB(peer_resp.error().status());
    } else if (req->return_dump_as_string()) {
      dump += peer_resp.dump();
    }
  }

  if (req->return_dump_as_string()) {
    resp->set_dump(resp->dump() + dump);
  }
  return Status::OK();
}

void CatalogManager::ReportMetrics() {
  // Report metrics on load balancer state.
  load_balance_policy_->ReportMetrics();

  // Report metrics on how many tservers are alive.
  auto num_live_servers = master_->ts_manager()->NumLiveDescriptors();
  metric_num_tablet_servers_live_->set_value(narrow_cast<uint32_t>(num_live_servers));
  auto num_servers = master_->ts_manager()->NumDescriptors();
  metric_num_tablet_servers_dead_->set_value(
      narrow_cast<uint32_t>(num_servers - num_live_servers));

  // Report the max master follower heartbeat delay.
  auto consensus_result = tablet_peer()->GetConsensus();
  if (consensus_result) {
    MonoTime earliest_time = MonoTime::kUninitialized;
    for (const auto& last_communication_time :
         (*consensus_result)->GetFollowerCommunicationTimes()) {
      earliest_time.MakeAtMost(last_communication_time.last_successful_communication);
    }
    int64_t time_in_ms =
        earliest_time ? MonoTime::Now().GetDeltaSince(earliest_time).ToMilliseconds() : 0;
    if (time_in_ms < 0) {
      time_in_ms = 0;
    }
    metric_max_follower_heartbeat_delay_->set_value(static_cast<uint64_t>(time_in_ms));
  }
}

void CatalogManager::ResetMetrics() {
  metric_num_tablet_servers_live_->set_value(0);
  metric_num_tablet_servers_dead_->set_value(0);
}


std::string CatalogManager::LogPrefix() const {
  if (tablet_peer()) {
    return consensus::MakeTabletLogPrefix(
        tablet_peer()->tablet_id(), tablet_peer()->permanent_uuid());
  } else {
    return consensus::MakeTabletLogPrefix(
        kSysCatalogTabletId, master_->fs_manager()->uuid());
  }
}

void CatalogManager::SetLoadBalancerEnabled(bool is_enabled) {
  load_balance_policy_->SetLoadBalancerEnabled(is_enabled);
}

bool CatalogManager::IsLoadBalancerEnabled() {
  return load_balance_policy_->IsLoadBalancerEnabled();
}

MonoDelta CatalogManager::TimeSinceElectedLeader() {
  return MonoTime::Now() - time_elected_leader_.load();
}

Status CatalogManager::GoIntoShellMode() {
  if (master_->IsShellMode()) {
    return STATUS(IllegalState, "Master is already in shell mode.");
  }

  LOG(INFO) << "Starting going into shell mode.";
  master_->SetShellMode(true);

  {
    LockGuard lock(mutex_);
    RETURN_NOT_OK(sys_catalog_->GoIntoShellMode());
    background_tasks_->Shutdown();
    background_tasks_.reset();
  }
  {
    std::lock_guard l(remote_bootstrap_mtx_);
    tablet_exists_ = false;
  }

  LOG(INFO) << "Done going into shell mode.";

  return Status::OK();
}

Result<SysClusterConfigEntryPB> CatalogManager::GetClusterConfig() {
  auto cluster_config = ClusterConfig();
  SCHECK_NOTNULL(cluster_config);
  return cluster_config->LockForRead()->pb;
}

Result<int32_t> CatalogManager::GetClusterConfigVersion() {
  auto cluster_config = ClusterConfig();
  SCHECK_NOTNULL(cluster_config);
  auto l = cluster_config->LockForRead();
  return l->pb.version();
}

Status CatalogManager::ValidateReplicationInfo(
    const ValidateReplicationInfoRequestPB* req, ValidateReplicationInfoResponsePB* resp) {
  TSDescriptorVector all_ts_descs;
  {
    BlacklistSet blacklist = VERIFY_RESULT(BlacklistSetFromPB());
    master_->ts_manager()->GetAllLiveDescriptors(&all_ts_descs, blacklist);
  }
  // We don't need any validation checks for read replica placements
  // because they aren't a part of any raft quorum underneath.
  // Technically, it is ok to have even 0 read replica nodes for them upfront.
  // We only need it for the primary cluster replicas.
  auto placement_info = req->replication_info().live_replicas();
  TSDescriptorVector ts_descs;
  // If the placement_info's uuid is empty, set it to be the current cluster's live replica uuid.
  if (placement_info.placement_uuid().empty()) {
    placement_info.set_placement_uuid(VERIFY_RESULT(placement_uuid()));
  }
  GetTsDescsFromPlacementInfo(placement_info, all_ts_descs, &ts_descs);
  Status s = CheckValidPlacementInfo(placement_info, ts_descs, resp);
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_TABLE_REPLICATION_INFO, s);
  }

  s = CatalogManagerUtil::CheckValidLeaderAffinity(req->replication_info());
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::INVALID_TABLE_REPLICATION_INFO, s);
  }

  return Status::OK();
}

Result<size_t> CatalogManager::GetReplicationFactor() {
  auto cluster_config = ClusterConfig();
  DCHECK(cluster_config) << "Missing cluster config for master!";
  auto l = cluster_config->LockForRead();
  const ReplicationInfoPB& replication_info = l->pb.replication_info();
  return GetNumReplicasOrGlobalReplicationFactor(replication_info.live_replicas());
}


Result<size_t> CatalogManager::GetTableReplicationFactor(const TableInfoPtr& table) const {
  auto replication_info = CatalogManagerUtil::GetTableReplicationInfo(
      table, GetTablespaceManager(), ClusterConfig()->LockForRead()->pb.replication_info());
  if (replication_info.has_live_replicas()) {
    return GetNumReplicasOrGlobalReplicationFactor(replication_info.live_replicas());
  }
  return FLAGS_replication_factor;
}

Result<size_t> CatalogManager::GetNumTabletReplicas(const TabletInfoPtr& tablet) {
  // For system tables, the set of replicas is always the set of masters.
  if (system_tablets_.find(tablet->id()) != system_tablets_.end()) {
    consensus::ConsensusStatePB master_consensus;
    RETURN_NOT_OK(GetCurrentConfig(&master_consensus));
    return master_consensus.config().peers().size();
  }
  int num_live_replicas = 0, num_read_replicas = 0;
  RETURN_NOT_OK(
      GetExpectedNumberOfReplicasForTablet(tablet->id(), &num_live_replicas, &num_read_replicas));
  return num_live_replicas + num_read_replicas;
}

Status CatalogManager::GetExpectedNumberOfReplicasForTablet(
    const TabletId& tablet_id, int* num_live_replicas, int* num_read_replicas) {
  TabletInfoPtr tablet_info;
  {
    SharedLock lock(mutex_);
    if (!FindCopy(*tablet_map_, tablet_id, &tablet_info)) {
      return STATUS_SUBSTITUTE(NotFound, "Unknown tablet $0", tablet_id);
    }
  }
  GetExpectedNumberOfReplicasForTable(tablet_info->table(), num_live_replicas, num_read_replicas);
  return Status::OK();
}

void CatalogManager::GetExpectedNumberOfReplicasForTable(
    const scoped_refptr<TableInfo>& table, int* num_live_replicas, int* num_read_replicas) {
  auto l = ClusterConfig()->LockForRead();
  auto replication_info = CatalogManagerUtil::GetTableReplicationInfo(
      table, GetTablespaceManager(), l->pb.replication_info());
  *num_live_replicas = GetNumReplicasOrGlobalReplicationFactor(replication_info.live_replicas());
  for (const auto& read_replica_placement_info : replication_info.read_replicas()) {
    *num_read_replicas += read_replica_placement_info.num_replicas();
  }
}

Result<string> CatalogManager::placement_uuid() const {
  auto cluster_config = ClusterConfig();
  if (!cluster_config) {
    return STATUS(IllegalState, "Missing cluster config for master!");
  }
  auto l = cluster_config->LockForRead();
  const ReplicationInfoPB& replication_info = l->pb.replication_info();
  return replication_info.live_replicas().placement_uuid();
}

Status CatalogManager::IsLoadBalanced(const IsLoadBalancedRequestPB* req,
                                      IsLoadBalancedResponsePB* resp) {
  if (req->has_expected_num_servers()) {
    TSDescriptorVector ts_descs;
    master_->ts_manager()->GetAllLiveDescriptors(&ts_descs);

    if (implicit_cast<size_t>(req->expected_num_servers()) > ts_descs.size()) {
      Status s = STATUS_SUBSTITUTE(IllegalState,
          "Found $0, which is below the expected number of servers $1.",
          ts_descs.size(), req->expected_num_servers());
      return SetupError(resp->mutable_error(), MasterErrorPB::CAN_RETRY_LOAD_BALANCE_CHECK, s);
    }
  }

  Status s = load_balance_policy_->IsIdle();
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::CAN_RETRY_LOAD_BALANCE_CHECK, s);
  }

  return Status::OK();
}

MonoTime CatalogManager::LastLoadBalancerRunTime() const {
  return load_balance_policy_->LastRunTime();
}

Status CatalogManager::IsLoadBalancerIdle(
    const IsLoadBalancerIdleRequestPB* req, IsLoadBalancerIdleResponsePB* resp) {
  Status s = load_balance_policy_->IsIdle();
  if (!s.ok()) {
    return SetupError(resp->mutable_error(), MasterErrorPB::LOAD_BALANCER_RECENTLY_ACTIVE, s);
  }

  return Status::OK();
}

int64_t CatalogManager::GetNumRelevantReplicas(const BlacklistPB& blacklist, bool leaders_only) {
  int64_t res = 0;
  SharedLock lock(mutex_);
  for (const auto& [_, tablet] : *tablet_map_) {
    auto l = tablet->LockForRead();
    // Not checking being created on purpose as we do not want initial load to be under accounted.
    if (!tablet->table() ||
        PREDICT_FALSE(l->is_deleted())) {
      continue;
    }

    auto locs = tablet->GetReplicaLocations();
    for (const auto& replica : *locs) {
      if (leaders_only && replica.second.role != PeerRole::LEADER) {
        continue;
      }
      for (const auto& host : blacklist.hosts()) {
        auto ts_desc_ptr = replica.second.ts_desc.lock();
        if (ts_desc_ptr && ts_desc_ptr->IsRunningOn(host)) {
          ++res;
          break;
        }
      }
    }
  }

  return res;
}

void CatalogManager::ResetTasksTrackers() {
  VLOG_WITH_FUNC(1) << "Begin";

  // Reset the jobs/tasks tracker.
  tasks_tracker_->Reset();
  jobs_tracker_->Reset();

  VLOG_WITH_FUNC(1) << "End";
}

void CatalogManager::AbortAndWaitForAllTasks() {
  std::vector<scoped_refptr<TableInfo>> tables;
  std::vector<scoped_refptr<NamespaceInfo>> namespaces;
  {
    SharedLock lock(mutex_);
    auto tables_it = tables_->GetAllTables();
    tables = std::vector(std::begin(tables_it), std::end(tables_it));
    namespaces = namespace_names_mapper_.GetAll();
  }
  CatalogEntityWithTasks::CloseAbortAndWaitForAllTasks(tables, /* call_task_finisher */ true);
  CatalogEntityWithTasks::CloseAbortAndWaitForAllTasks(namespaces, /* call_task_finisher*/ true);
}

void CatalogManager::AbortAndWaitForAllTasksUnlocked(bool call_task_finisher) {
  CatalogEntityWithTasks::CloseAbortAndWaitForAllTasks(tables_->GetAllTables(), call_task_finisher);
  CatalogEntityWithTasks::CloseAbortAndWaitForAllTasks(
      namespace_names_mapper_.GetAll(), call_task_finisher);
}

void CatalogManager::HandleNewTableId(const TableId& table_id) {
  ysql_manager_->HandleNewTableId(table_id);
}

scoped_refptr<TableInfo> CatalogManager::NewTableInfo(TableId id, bool colocated) {
  return make_scoped_refptr<TableInfo>(id, colocated, tasks_tracker_);
}

Status CatalogManager::ScheduleTask(std::shared_ptr<server::RunnableMonitoredTask> task) {
  RETURN_NOT_OK(task->BeforeSubmitToTaskPool());
  Status s = async_task_pool_->SubmitFunc([task]() {
      WARN_NOT_OK(task->Run(), "Failed task");
  });
  // If we are not able to enqueue, abort the task.
  if (!s.ok()) {
    RETURN_NOT_OK(task->OnSubmitFailure());
    task->AbortAndReturnPrevState(s, /* call_task_finisher */ true);
  }
  return s;
}

Status CatalogManager::CollectTable(
    const TableDescription& table_description,
    CollectFlags flags,
    std::vector<TableDescription>* all_tables,
    std::unordered_set<TableId>* parent_colocated_table_ids) {
  auto lock = table_description.table_info->LockForRead();
  if (lock->started_hiding()) {
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Rejected hidden table: " << AsString(table_description.table_info);
    return Status::OK();
  }
  if (lock->started_deleting()) {
    VLOG_WITH_PREFIX_AND_FUNC(4)
        << "Rejected deleted table: " << AsString(table_description.table_info);
    return Status::OK();
  }
  if (flags.Test(CollectFlag::kIncludeParentColocatedTable) && lock->pb.colocated()) {
    bool add_parent = !IsColocationParentTableId(table_description.table_info->id());
    if (add_parent && lock->is_vector_index()) {
      auto indexed_table = VERIFY_RESULT(FindTableById(lock->indexed_table_id()));
      add_parent = indexed_table->LockForRead()->pb.colocated();
    }
    if (add_parent) {
      // If a table is colocated, add its parent colocated table as well.
      TableId parent_table_id = VERIFY_RESULT(
          GetParentTableIdForColocatedTable(table_description.table_info));

      auto result = parent_colocated_table_ids->insert(parent_table_id);
      if (result.second) {
        // We have not processed this parent table id yet, so do that now.
        TableIdentifierPB parent_table_pb;
        parent_table_pb.set_table_id(parent_table_id);
        parent_table_pb.mutable_namespace_()->set_id(table_description.namespace_info->id());
        all_tables->push_back(VERIFY_RESULT(DescribeTable(
            parent_table_pb, flags.Test(CollectFlag::kSucceedIfCreateInProgress))));
      }
    }
  }
  // Avoid adding colocation parent tables (tablegroups and colocated database) twice.
  if (!IsColocationParentTableId(table_description.table_info->id()) ||
      parent_colocated_table_ids->insert(table_description.table_info->id()).second) {
    all_tables->push_back(table_description);
  }
  if (flags.Test(CollectFlag::kAddIndexes)) {
    TRACE(Substitute("Locking object with id $0", table_description.table_info->id()));

    if (lock->is_index()) {
      return STATUS(InvalidArgument, "Expected table, but found index",
                    table_description.table_info->id(),
                    MasterError(MasterErrorPB::INVALID_TABLE_TYPE));
    }

    if (lock->table_type() == PGSQL_TABLE_TYPE) {
      return STATUS(InvalidArgument, "Getting indexes for YSQL table is not supported",
                    table_description.table_info->id(),
                    MasterError(MasterErrorPB::INVALID_TABLE_TYPE));
    }

    auto collect_index_flags = flags;
    // Don't need to collect indexes for index.
    collect_index_flags.Reset(CollectFlag::kAddIndexes);
    for (const auto& index_info : lock->pb.indexes()) {
      LOG_IF(DFATAL, table_description.table_info->id() != index_info.indexed_table_id())
              << "Wrong indexed table id in index descriptor";
      TableIdentifierPB index_id_pb;
      index_id_pb.set_table_id(index_info.table_id());
      index_id_pb.mutable_namespace_()->set_id(table_description.namespace_info->id());
      auto index_description = VERIFY_RESULT(DescribeTable(
          index_id_pb, flags.Test(CollectFlag::kSucceedIfCreateInProgress)));
      RETURN_NOT_OK(CollectTable(
          index_description, collect_index_flags, all_tables, parent_colocated_table_ids));
    }
  }

  return Status::OK();
}

Result<vector<TableDescription>> CatalogManager::CollectTables(
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& table_identifiers,
    CollectFlags flags,
    std::unordered_set<NamespaceId>* namespaces) {
  std::vector<std::pair<TableInfoPtr, CollectFlags>> table_with_flags;

  {
    SharedLock lock(mutex_);
    for (const auto& table_id_pb : table_identifiers) {
      if (table_id_pb.table_name().empty() && table_id_pb.table_id().empty() &&
          table_id_pb.has_namespace_()) {
        // Collect all tables belonging to a namespace. Happens when only the namespace is specified
        auto namespace_info = FindNamespaceUnlocked(table_id_pb.namespace_());
        if (!namespace_info.ok()) {
          VLOG_WITH_PREFIX_AND_FUNC(1)
              << "Namespace not found: " << table_id_pb.namespace_().ShortDebugString()
              << ", status: "<< namespace_info.status().ToString();
          if (namespace_info.status().IsNotFound()) {
            continue;
          }
          return namespace_info.status();
        }
        if (namespaces) {
          namespaces->insert((**namespace_info).id());
        }


        auto ns_collect_flags = flags;
        // Don't collect indexes, since they should be in the same namespace and will be collected
        // as regular tables.
        // It is necessary because we don't support kAddIndexes for YSQL tables.
        ns_collect_flags.Reset(CollectFlag::kAddIndexes);
        VLOG_WITH_PREFIX_AND_FUNC(1)
            << "Collecting all tables from: " << (**namespace_info).ToString() << ", specified as: "
            << table_id_pb.namespace_().ShortDebugString();
        for (const auto& table : tables_->GetAllTables()) {
          if (table->is_system()) {
            VLOG_WITH_PREFIX_AND_FUNC(4) << "Rejected system table: " << AsString(table);
            continue;
          }
          auto lock = table->LockForRead();
          if (lock->namespace_id() != (**namespace_info).id()) {
            VLOG_WITH_PREFIX_AND_FUNC(4)
                << "Rejected table from other namespace: " << AsString(table);
            continue;
          }
          VLOG_WITH_PREFIX_AND_FUNC(4) << "Accepted: " << AsString(table);
          table_with_flags.emplace_back(table, ns_collect_flags);
        }
      } else {
        auto table = VERIFY_RESULT(FindTableUnlocked(table_id_pb));
        VLOG_WITH_PREFIX_AND_FUNC(1) << "Collecting table: " << table->ToString();
        table_with_flags.emplace_back(table, flags);
      }
    }
  }

  std::sort(table_with_flags.begin(), table_with_flags.end(), [](const auto& p1, const auto& p2) {
    return p1.first->id() < p2.first->id();
  });
  std::vector<TableDescription> all_tables;
  std::unordered_set<TableId> parent_colocated_table_ids;
  const TableId* table_id = nullptr;
  for (auto& table_and_flags : table_with_flags) {
    if (table_id && *table_id == table_and_flags.first->id()) {
      return STATUS_FORMAT(InternalError, "Table collected twice $0", *table_id);
    }
    auto description = VERIFY_RESULT(DescribeTable(
        table_and_flags.first,
        table_and_flags.second.Test(CollectFlag::kSucceedIfCreateInProgress)));
    RETURN_NOT_OK(CollectTable(
        description, table_and_flags.second, &all_tables, &parent_colocated_table_ids));
    table_id = &table_and_flags.first->id();
  }

  return all_tables;
}

Result<std::vector<TableDescription>> CatalogManager::CollectTables(
    const google::protobuf::RepeatedPtrField<TableIdentifierPB>& table_identifiers,
    bool add_indexes,
    bool include_parent_colocated_table) {
  CollectFlags flags;
  flags.SetIf(CollectFlag::kAddIndexes, add_indexes);
  flags.SetIf(CollectFlag::kIncludeParentColocatedTable, include_parent_colocated_table);
  return CollectTables(table_identifiers, flags);
}

Status CatalogManager::GetYQLPartitionsVTable(std::shared_ptr<SystemTablet>* tablet) {
  scoped_refptr<TableInfo> table = FindPtrOrNull(table_names_map_,
      std::make_pair(kSystemNamespaceId, kSystemPartitionsTableName));
  SCHECK(table != nullptr, NotFound, "YQL system.partitions table not found");

  auto tablets = VERIFY_RESULT(table->GetTablets());
  SCHECK(tablets.size() == 1, NotFound, "YQL system.partitions tablet not found");
  *tablet = std::dynamic_pointer_cast<SystemTablet>(
      VERIFY_RESULT(GetSystemTablet(tablets[0]->tablet_id())));
  return Status::OK();
}

void CatalogManager::RebuildYQLSystemPartitions() {
  // This task will keep running, but only want it to do anything if
  // FLAGS_partitions_vtable_cache_refresh_secs is explicitly set to some value >0.
  const bool use_bg_thread_to_update_cache =
      YQLPartitionsVTable::ShouldGeneratePartitionsVTableOnChanges() &&
      FLAGS_partitions_vtable_cache_refresh_secs > 0;

  if (YQLPartitionsVTable::ShouldGeneratePartitionsVTableWithBgTask() ||
      use_bg_thread_to_update_cache) {
    SCOPED_LEADER_SHARED_LOCK(l, this);
    if (l.IsInitializedAndIsLeader()) {
      if (system_partitions_tablet_ != nullptr) {
        Status s = ResultToStatus(GetYqlPartitionsVtable().GenerateAndCacheData());
        if (!s.ok()) {
          LOG(ERROR) << "Error rebuilding system.partitions: " << s.ToString();
        }
      } else {
        LOG(ERROR) << "Error finding system.partitions vtable.";
      }
    }
  }

  auto wait_time = FLAGS_partitions_vtable_cache_refresh_secs * 1s;
  if (wait_time <= 0s) {
    wait_time = kDefaultYQLPartitionsRefreshBgTaskSleep;
  }
  refresh_yql_partitions_task_.Schedule([this](const Status& status) {
    WARN_NOT_OK(
        background_tasks_thread_pool_->SubmitFunc([this]() { RebuildYQLSystemPartitions(); }),
        "Failed to schedule: RebuildYQLSystemPartitions");
  }, wait_time);
}

Status CatalogManager::SysCatalogRespectLeaderAffinity() {
  auto l = ClusterConfig()->LockForRead();

  auto blacklist = ToBlacklistSet(GetBlacklist(l->pb, /*leader_blacklist=*/true));
  bool i_am_blacklisted = IsBlacklisted(server_registration_, blacklist);

  vector<AffinitizedZonesSet> affinitized_zones;
  RETURN_NOT_OK(GetAllAffinitizedZones(&affinitized_zones));

  if (affinitized_zones.empty() && !i_am_blacklisted) {
    return Status::OK();
  }

  std::vector<ServerEntryPB> masters;
  RETURN_NOT_OK(master_->ListMasters(&masters));

  auto[sorted_scored_masters, i_am_eligible] = GetMoreEligibleSysCatalogLeaders(
      affinitized_zones, blacklist, masters, server_registration_);
  for (const auto& scored_master : sorted_scored_masters) {
    YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 10) << Format(
        "Sys catalog tablet leader is $0. Stepping down to master uuid $1 in zone $2.",
        (i_am_blacklisted ? "blacklisted" : "not in an affinitized zone"),
        scored_master.first.instance_id().permanent_uuid(),
        TSDescriptor::generate_placement_id(scored_master.first.registration().cloud_info()));
    if (VERIFY_RESULT(SysCatalogLeaderStepDown(scored_master.first))) {
      return Status::OK();
    }
  }
  if (!i_am_eligible)
    return STATUS(NotFound, "Couldn't step down to an unblacklisted master in an affinitized zone");
  else
    return Status::OK();
}

Status CatalogManager::GetAllAffinitizedZones(vector<AffinitizedZonesSet>* affinitized_zones) {
  SysClusterConfigEntryPB config = VERIFY_RESULT(GetClusterConfig());
  auto& replication_info = config.replication_info();

  CatalogManagerUtil::GetAllAffinitizedZones(replication_info, affinitized_zones);

  return Status::OK();
}

Result<BlacklistSet> CatalogManager::BlacklistSetFromPB(bool leader_blacklist) const {
  auto cluster_config = ClusterConfig();
  if (!cluster_config) {
    return STATUS(IllegalState, "Cluster config not found.");
  }
  auto l = cluster_config->LockForRead();

  return ToBlacklistSet(GetBlacklist(l->pb, leader_blacklist));
}

void CatalogManager::CheckTableDeleted(const TableInfoPtr& table, const LeaderEpoch& epoch) {
  // Since this is called after every successful async DeleteTablet, it's possible if all tasks
  // complete, for us to mark the table as DELETED/HIDDEN asap. This is desirable as clients will
  // wait for this before returning success to the user.
  //
  // However, if tasks fail, timeout, or are aborted, we still have the background thread as a
  // catch all.
  if (!ShouldDeleteTable(table)) {
    return;
  }

  WARN_NOT_OK(async_task_pool_->SubmitFunc([this, table, epoch]() {
    auto lock = PrepareTableDeletion(table);
    if (!lock.locked()) {
      return;
    }
    // Clean up any DDL verification state that is waiting for this table to be deleted.
    auto res = table->LockForRead()->GetCurrentDdlTransactionId();
    WARN_NOT_OK(
        res, "Failed to get current DDL transaction for table " + table->ToString());
    bool need_remove_ddl_state = false;
    if (res.ok() && res.get() != TransactionId::Nil()) {
      // When deleting an index, we also need to update the indexed table
      // to remove this index from it. Updating the indexed table involves
      // setting up a fully_applied_schema and incrementing its schema version.
      // Then the indexed table is altered with the new schema asynchronously.
      // It is possible that the next statement after this drop index statement
      // starts too soon and reads the fully_applied_schema_version which is
      // the old schema version of the indexed table and later fails with
      // "schema version mismatch" error. To mitigate this error, we wait
      // for the indexed table's fully_applied_schema to be cleared or until
      // a maximum number of iterations.
      // Note that if res.get() == TransactionId::Nil() it means DDL atomicity
      // is disabled, in this case PgClientSession::DropTable will call
      // WaitUntilIndexPermissionsAtLeast which takes care of the purpose of
      // doing this wait. Therefore this wait only applies when DDL atomicity
      // is enabled.
      if (table->is_index()) {
        auto table_id = table->indexed_table_id();
        WARN_NOT_OK(
          WaitFor(
              [this, &table_id]() -> Result<bool> {
                auto indexed_table = GetTableInfo(table_id);
                return !indexed_table ||
                       !indexed_table->LockForRead()->pb.has_fully_applied_schema();
              },
              20s,
              Format("Waiting for fully_applied_schema of $0 to clear", table_id),
              500ms /* initial_delay */,
              1.0 /* delay_multiplier */),
          Format("fully_applied_schema of $0 fail to clear", table_id));
      }
      VLOG(3) << "Check table deleted " << table->id();
      need_remove_ddl_state = true;
      lock.mutable_data()->pb.clear_ysql_ddl_txn_verifier_state();
      lock.mutable_data()->pb.clear_transaction();
    }
    Status s = sys_catalog_->Upsert(epoch, table);
    if (!s.ok()) {
      LOG_WITH_PREFIX(WARNING)
          << "Error marking table as "
          << (lock.data().started_deleting() ? "DELETED" : "HIDDEN") << ": " << s;
      return;
    }
    lock.Commit();
    if (need_remove_ddl_state) {
      RemoveDdlTransactionState(table->id(), {res.get()});
    }
  }), "Failed to submit update table task");
}

const YQLPartitionsVTable& CatalogManager::GetYqlPartitionsVtable() const {
  return down_cast<const YQLPartitionsVTable&>(system_partitions_tablet_->YQLTable());
}

Status CatalogManager::InitializeTableLoadState(
    const TableId& table_id, TSDescriptorVector ts_descs, CMPerTableLoadState* state) {
  for (const auto& ts : ts_descs) {
    // Touch every tserver with 0 load.
    state->per_ts_replica_load_[ts->permanent_uuid()];
    // Insert into the sorted list.
    state->sorted_replica_load_.emplace_back(ts->permanent_uuid());
  }

  auto table_info = GetTableInfo(table_id);

  if (!table_info) {
    return Status::OK();
  }
  return CatalogManagerUtil::FillTableLoadState(table_info, state);
}

// TODO: consider unifying this code with the load balancer.
Result<CMGlobalLoadState> CatalogManager::InitializeGlobalLoadState(
    const TSDescriptorVector& ts_descs) {
  CMGlobalLoadState state;
  for (const auto& ts : ts_descs) {
    // Touch every tserver with 0 load.
    state.per_ts_replica_load_[ts->permanent_uuid()];
    state.per_ts_protege_load_[ts->permanent_uuid()];
  }

  SharedLock l(mutex_);
  for (const auto& info : tables_->GetAllTables()) {
    // Ignore system, colocated and deleting/deleted tables.
    {
      auto l = info->LockForRead();
      if (info->is_system() ||
          info->IsColocatedUserTable() ||
          l->started_deleting()) {
        continue;
      }
    }
    RETURN_NOT_OK(CatalogManagerUtil::FillTableLoadState(info, &state));
  }
  return state;
}

Result<bool> CatalogManager::SysCatalogLeaderStepDown(const ServerEntryPB& master) {
    if (PREDICT_FALSE(
            GetAtomicFlag(&FLAGS_TEST_crash_server_on_sys_catalog_leader_affinity_move))) {
      LOG_WITH_PREFIX(FATAL) << "For test: Crashing the server instead of performing sys "
                                "catalog leader affinity move.";
    }
  auto tablet_peer = VERIFY_RESULT(GetServingTablet(sys_catalog_->tablet_id()));

  consensus::LeaderStepDownRequestPB req;
  req.set_tablet_id(sys_catalog_->tablet_id());
  req.set_dest_uuid(sys_catalog_->tablet_peer()->permanent_uuid());
  req.set_new_leader_uuid(master.instance_id().permanent_uuid());

  consensus::LeaderStepDownResponsePB resp;
  RETURN_NOT_OK(VERIFY_RESULT(tablet_peer->GetConsensus())->StepDown(&req, &resp));
  if (resp.has_error()) {
    YB_LOG_WITH_PREFIX_EVERY_N_SECS(INFO, 10)
        << "Step down failed: " << resp.error().status().message();
    return false;
  }
  LOG_WITH_PREFIX(INFO) << "Successfully stepped down to new master";
  return true;
}

std::shared_ptr<ClusterConfigInfo> CatalogManager::ClusterConfig() const {
  return cluster_config_;
}

Status CatalogManager::TryRemoveFromTablegroup(const TableId& table_id) {
  LockGuard lock(mutex_);
  auto tablegroup = tablegroup_manager_->FindByTable(table_id);
  if (tablegroup) {
    RETURN_NOT_OK(tablegroup->RemoveChildTable(table_id));
  }
  return Status::OK();
}

Status CatalogManager::CheckIfPitrActive(
    const CheckIfPitrActiveRequestPB* req, CheckIfPitrActiveResponsePB* resp) {
  LOG(INFO) << "Servicing CheckIfPitrActive request";
  resp->set_is_pitr_active(IsPitrActive());
  return Status::OK();
}

Result<TableId> CatalogManager::GetParentTableIdForColocatedTable(
    const scoped_refptr<TableInfo>& table) {
  SharedLock lock(mutex_);
  return GetParentTableIdForColocatedTableUnlocked(table);
}

Result<TableId> CatalogManager::GetParentTableIdForColocatedTableUnlocked(
    const TableInfoPtr& table) {
  DCHECK(table->colocated());
  DCHECK(!IsColocationParentTableId(table->id()));

  auto table_lock = table->LockForRead();
  auto ns_info = VERIFY_RESULT(FindNamespaceByIdUnlocked(table_lock->namespace_id()));

  auto tablegroup = tablegroup_manager_->FindByTable(table->id());
  if (ns_info->colocated()) {
    // Two types of colocated database: (1) pre-Colocation GA (2) Colocation GA
    // Colocated databases created before Colocation GA don't use tablegroup
    // to manage its colocated tables.
    if (tablegroup) {
      return GetColocationParentTableId(tablegroup->id());
    }
    return GetColocatedDbParentTableId(table_lock->namespace_id());
  }
  RSTATUS_DCHECK(tablegroup != nullptr,
                 Corruption,
                 Format("Not able to find the tablegroup for a colocated table $0 whose database "
                        "is not colocated.", table->id()));
  return GetTablegroupParentTableId(tablegroup->id());
}

Result<std::optional<cdc::ConsumerRegistryPB>> CatalogManager::GetConsumerRegistry() {
  auto cluster_config = ClusterConfig();
  if (!cluster_config) {
    return STATUS(IllegalState, "Cluster config is not initialized");
  }
  auto l = cluster_config->LockForRead();
  if (l->pb.has_consumer_registry()) {
    return l->pb.consumer_registry();
  }

  return std::nullopt;
}

AsyncTaskThrottlerBase* CatalogManager::GetDeleteReplicaTaskThrottler(
    const string& ts_uuid) {

  // The task throttlers are owned by the CatalogManager. First, check if it exists while holding a
  // read lock.
  {
    SharedLock l(delete_replica_task_throttler_per_ts_mutex_);
    if (delete_replica_task_throttler_per_ts_.count(ts_uuid) == 1) {
      return delete_replica_task_throttler_per_ts_.at(ts_uuid).get();
    }
  }

  // A task throttler does not exist for the given tserver uuid. Create one while holding a write
  // lock.
  LockGuard lock(delete_replica_task_throttler_per_ts_mutex_);
  if (delete_replica_task_throttler_per_ts_.count(ts_uuid) == 0) {
    delete_replica_task_throttler_per_ts_.emplace(
      ts_uuid,
      std::make_unique<DynamicAsyncTaskThrottler>([]() {
        return GetAtomicFlag(&FLAGS_max_concurrent_delete_replica_rpcs_per_ts);
      }));
  }

  return delete_replica_task_throttler_per_ts_.at(ts_uuid).get();
}

Status CatalogManager::SubmitToSysCatalog(std::unique_ptr<tablet::Operation> operation) {
  auto tablet = VERIFY_RESULT(tablet_peer()->shared_tablet_safe());
  operation->SetTablet(tablet);
  tablet_peer()->Submit(std::move(operation), tablet_peer()->LeaderTerm());
  return Status::OK();
}

Status CatalogManager::GetStatefulServiceLocation(
    const GetStatefulServiceLocationRequestPB* req, GetStatefulServiceLocationResponsePB* resp) {
  VLOG(4) << "GetStatefulServiceLocation: " << req->ShortDebugString();

  SCHECK(req->has_service_kind(), InvalidArgument, "Service kind is not specified");

  const auto service_table_name = GetStatefulServiceTableName(req->service_kind());

  TableIdentifierPB table_identifier;
  table_identifier.set_table_name(service_table_name);
  table_identifier.mutable_namespace_()->set_name(kSystemNamespaceName);

  auto table_result = FindTable(table_identifier);
  if (!VERIFY_RESULT(DoesTableExist(table_result)) || table_result.get()->IsCreateInProgress()) {
    return SetupError(
        resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND,
        STATUS_FORMAT(NotFound, "Stateful Service of kind $0 does not exist", req->service_kind()));
  }

  auto& table = table_result.get();

  auto tablets = VERIFY_RESULT(table->GetTablets());
  if (tablets.size() != 1) {
    DCHECK(false) << "Stateful Service of kind " << req->service_kind()
                  << " has more than one tablet: " << tablets.size();
    return SetupError(
        resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND,
        STATUS_FORMAT(
            NotFound, "Stateful Service of kind $0 expected to have one tablet, but $1 were found",
            req->service_kind(), tablets.size()));
  }

  auto ts_result = tablets[0]->GetLeader();
  if (!ts_result.ok()) {
    return SetupError(
        resp->mutable_error(), MasterErrorPB::OBJECT_NOT_FOUND,
        STATUS_FORMAT(
            TryAgain, "Leader not found for Stateful Service of kind $0: ", req->service_kind(),
            ts_result.status().ToString()));
  }
  auto ts = *ts_result;

  auto* service_info = resp->mutable_service_info();
  service_info->set_permanent_uuid(ts->permanent_uuid());
  auto registration = ts->GetRegistration();
  service_info->mutable_private_rpc_addresses()->CopyFrom(registration.private_rpc_addresses());
  service_info->mutable_broadcast_addresses()->CopyFrom(registration.broadcast_addresses());
  service_info->mutable_cloud_info()->CopyFrom(registration.cloud_info());

  return Status::OK();
}

void CatalogManager::Started() {
  master_->snapshot_coordinator().Start();
}

void CatalogManager::SysCatalogLoaded(SysCatalogLoadingState&& state) {
  ValidateIndexTablesPostLoad(std::move(state.validate_backfill_status_index_tables),
                              &state.write_to_disk_tables);

  StartPostLoadTasks(std::move(state.post_load_tasks));
  StartWriteTableToSysCatalogTasks(std::move(state.write_to_disk_tables));

  if (FLAGS_ysql_enable_db_catalog_version_mode && FLAGS_enable_ysql) {
    // Initialize the catalog version cache.
    // This is needed for cases like ysql major upgrade where master runs a postgres process.
    DbOidToCatalogVersionMap versions;
    WARN_NOT_OK(
        GetYsqlAllDBCatalogVersions(false /* use_cache */, &versions, nullptr /* fingerprint */),
        "Failed to read all DB catalog versions");
  }

  ysql_manager_->SysCatalogLoaded(state.epoch);

  master_->snapshot_coordinator().SysCatalogLoaded(state.epoch.leader_term);

  xcluster_manager_->SysCatalogLoaded(state.epoch);
  SchedulePostTabletCreationTasksForPendingTables(state.epoch);
}

Status CatalogManager::UpdateLastFullCompactionRequestTime(
    const TableId& table_id, const LeaderEpoch& epoch) {
  auto table_info = VERIFY_RESULT(FindTableById(table_id));
  const auto request_time = master_->clock()->Now().ToUint64();
  auto lock = table_info->LockForWrite();
  lock.mutable_data()->pb.set_last_full_compaction_request_time(request_time);
  RETURN_NOT_OK(sys_catalog_->Upsert(epoch, table_info));
  lock.Commit();
  return Status::OK();
}

Status CatalogManager::GetCompactionStatus(
    const GetCompactionStatusRequestPB* req, GetCompactionStatusResponsePB* resp) {
  auto table_info = VERIFY_RESULT(FindTableById(req->table().table_id()));
  HybridTime last_request_time;
  HybridTime table_last_full_compaction_time;
  tablet::FullCompactionState table_compaction_state = tablet::IDLE;

  {
    auto lock = table_info->LockForRead();
    last_request_time = HybridTime(lock->pb.last_full_compaction_request_time());
  }

  const auto tablets = VERIFY_RESULT(table_info->GetTablets());
  // Find the compaction state of the table. If any one tablet is UNKNOWN, then the table is
  // UNKNOWN. Else if any one tablet is COMPACTING, then the table is COMPACTING.
  if (tablets.empty()) {
    table_compaction_state = tablet::FULL_COMPACTION_STATE_UNKNOWN;
  }

  for (const auto& tablet_info : tablets) {
    const auto replica_locations = tablet_info->GetReplicaLocations();
    if (table_compaction_state == tablet::FULL_COMPACTION_STATE_UNKNOWN) {
      break;
    }

    for (const auto& [_, replica] : *replica_locations) {
      const auto state = replica.full_compaction_status.full_compaction_state;
      const auto last_compact_time = replica.full_compaction_status.last_full_compaction_time;

      if (state == tablet::FULL_COMPACTION_STATE_UNKNOWN) {
        table_compaction_state = tablet::FULL_COMPACTION_STATE_UNKNOWN;
        break;
      }

      if (state == tablet::COMPACTING) {
        table_compaction_state = tablet::COMPACTING;
      }

      if (table_last_full_compaction_time.ToUint64() == 0) {
        table_last_full_compaction_time = last_compact_time;
      } else {
        // The table's last full compaction time is the time of the earliest replica to finish.
        table_last_full_compaction_time =
            std::min(table_last_full_compaction_time, last_compact_time);
      }
    }
  }

  resp->set_last_request_time(last_request_time.ToUint64());
  resp->set_last_full_compaction_time(table_last_full_compaction_time.ToUint64());
  resp->set_full_compaction_state(table_compaction_state);

  if (req->show_tablets()) {
    for (const auto& tablet_info : tablets) {
      const auto replica_locations = tablet_info->GetReplicaLocations();
      for (const auto& [ts_id, replica] : *replica_locations) {
        const auto state = replica.full_compaction_status.full_compaction_state;
        const auto last_compact_time = replica.full_compaction_status.last_full_compaction_time;

        auto* replica_status = resp->add_replica_statuses();
        replica_status->set_ts_id(ts_id);
        replica_status->set_tablet_id(tablet_info->id());
        replica_status->set_full_compaction_state(state);
        replica_status->set_last_full_compaction_time(last_compact_time.ToUint64());
      }
    }
  }

  return Status::OK();
}

void CatalogManager::StartPostLoadTasks(SysCatalogPostLoadTasks&& post_load_tasks) {
  for (auto& [task, msg] : post_load_tasks) {
    auto status = background_tasks_thread_pool_->SubmitFunc(std::move(task));
    if (status.ok()) {
      LOG(INFO) << "Successfully submitted post load task: " << msg;
    } else {
      LOG(WARNING) << Format("Failed to submit post load task: $0. Reason: $1", msg, status);
    }
  }
}

void CatalogManager::StartWriteTableToSysCatalogTasks(TableIdSet&& tables_to_persist) {
  // Check if some tables metadata requires writing to disk. Currently we are submitting one task
  // per table, alternatively we may submit one task to iterate through all the tables.
  for (auto& table_id : tables_to_persist) {
    auto status = background_tasks_thread_pool_->SubmitFunc(
        [this, table_id = std::move(table_id)]() { WriteTableToSysCatalog(table_id); });
    if (status.ok()) {
      LOG(INFO) << "Successfully submitted post load task: WriteTableToSysCatalog";
    } else {
      LOG(WARNING) << "Failed to submit post load task: WriteTableToSysCatalog. Reason: " << status;
    }
  }
}

void CatalogManager::WriteTableToSysCatalog(const TableId& table_id) {
  auto table_ptr = GetTableInfo(table_id);
  if (!table_ptr) {
    LOG_WITH_FUNC(WARNING) << Format("Could not find table $0 in table map.", table_id);
    return;
  }

  LOG_WITH_FUNC(INFO) << Format(
      "Writing table $0 to sys catalog as part of a migration.", table_id);
  auto l = table_ptr->LockForWrite();
  WARN_NOT_OK(sys_catalog_->ForceUpsert(leader_ready_term(), table_ptr),
      "Failed to upsert migrated table into sys catalog.");
}

void CatalogManager::WriteTabletToSysCatalog(const TabletId& tablet_id) {
  auto tablet_res = GetTabletInfo(tablet_id);
  if (!tablet_res.ok()) {
    LOG_WITH_FUNC(WARNING) << Format("Could not find tablet $1 in tablet map.", tablet_id);
    return;
  }

  LOG_WITH_FUNC(INFO) << Format(
      "Writing tablet $0 to sys catalog as part of a migration.", tablet_id);
  auto l = (*tablet_res)->LockForWrite();
  WARN_NOT_OK(sys_catalog_->ForceUpsert(leader_ready_term(), *tablet_res),
      "Failed to upsert migrated colocated tablet into sys catalog.");
}

void CatalogManager::SchedulePostTabletCreationTasks(
    const TableInfoPtr& table_info, const LeaderEpoch& epoch,
    const std::set<TabletId>& new_running_tablets) {
  auto& mutable_table_info = *table_info->mutable_metadata();
  DCHECK(mutable_table_info.HasWriteLock());

  if (!mutable_table_info.mutable_dirty()->IsPreparing()) {
    return;
  }
  auto tablets_running_result = table_info->AreAllTabletsRunning(new_running_tablets);
  if (!tablets_running_result.ok() || !*tablets_running_result) {
    return;
  }

  // Collect all PostTabletCreateTasks for this table.
  auto table_creation_tasks = xcluster_manager_->GetPostTabletCreateTasks(table_info, epoch);
  // TODO: Get cdcsdk tasks

  if (table_creation_tasks.empty()) {
    // Schedule a simple task that will mark this table as RUNNING when it completes.
    table_creation_tasks.emplace_back(std::make_shared<MarkTableAsRunningTask>(
        *this, *AsyncTaskPool(), *master_->messenger(), table_info, epoch));
  }

  WARN_NOT_OK(
      PostTabletCreateTaskBase::StartTasks(table_creation_tasks, this, table_info, epoch),
      "Failed to schedule PostTabletCreateTasks");
}

Status CatalogManager::PromoteTableToRunningState(
    TableInfoPtr table_info, const LeaderEpoch& epoch) {
  auto l = table_info->LockForWrite();
  SCHECK(
      l.mutable_data()->IsPreparing(), IllegalState,
      "Table $0 should be in PREPARING state. Current state: $1", table_info->ToString(),
      SysTablesEntryPB_State_Name(l.mutable_data()->pb.state()));
  l.mutable_data()->pb.set_state(SysTablesEntryPB::RUNNING);
  RETURN_NOT_OK_PREPEND(
      sys_catalog_->Upsert(epoch, table_info.get()), "Promote table to RUNNING state");
  l.Commit();
  return Status::OK();
}

void CatalogManager::SchedulePostTabletCreationTasksForPendingTables(const LeaderEpoch& epoch) {
  std::vector<TableId> table_ids;
  {
    SharedLock lock(mutex_);
    table_ids.reserve(tables_->Size());
    for (const auto& table_info : tables_->GetAllTables()) {
      table_ids.push_back(table_info->id());
    }
  }

  for (auto& table_id : table_ids) {
    auto table_info_result = FindTableById(table_id);
    if (!table_info_result) {
      LOG(WARNING) << "Failed to find table " << table_id << " to schedule PostTabletCreationTasks";
      continue;
    }
    auto& table_info = *table_info_result;
    auto wl = table_info->LockForWrite();
    SchedulePostTabletCreationTasks(table_info, epoch);
  }
}

void CatalogManager::StartPgCatalogVersionsBgTaskIfStopped() {
  // In per-database catalog version mode, if heartbeat PG catalog versions
  // cache is enabled, start a background task to periodically read the
  // pg_yb_catalog_version table and cache the result.
  if (FLAGS_ysql_enable_db_catalog_version_mode &&
      FLAGS_enable_heartbeat_pg_catalog_versions_cache) {
    const bool is_task_running = pg_catalog_versions_bg_task_running_.exchange(true);
    if (is_task_running) {
      // Task already running, nothing to do.
      return;
    }
    ScheduleRefreshPgCatalogVersionsTask(true /* schedule_now */);
  }
}

void CatalogManager::ScheduleRefreshPgCatalogVersionsTask(bool schedule_now) {
  // Schedule the next refresh catalog versions task. Do it twice every
  // tserver to master heartbeat so we have reasonably recent catalog versions
  // used for heartbeat response.
  auto wait_time = schedule_now ? 0 : (FLAGS_heartbeat_interval_ms / 2);
  refresh_ysql_pg_catalog_versions_task_.Schedule([this](const Status& status) {
    Status s = background_tasks_thread_pool_->SubmitFunc(
        [this]() { RefreshPgCatalogVersionInfoPeriodically(); });
    if (!s.ok()) {
      LOG(WARNING) << "Failed to schedule: " << __func__;
      pg_catalog_versions_bg_task_running_ = false;
      ResetCachedCatalogVersions();
    }
  }, wait_time * 1ms);
}

void CatalogManager::ResetCachedCatalogVersions() {
  LockGuard lock(heartbeat_pg_catalog_versions_cache_mutex_);
  if (heartbeat_pg_catalog_versions_cache_) {
    heartbeat_pg_catalog_versions_cache_->clear();
  }
}

void CatalogManager::RefreshPgCatalogVersionInfoPeriodically() {
  DCHECK(FLAGS_ysql_enable_db_catalog_version_mode);
  DCHECK(FLAGS_enable_heartbeat_pg_catalog_versions_cache);
  DCHECK(pg_catalog_versions_bg_task_running_);

  {
    SCOPED_LEADER_SHARED_LOCK(l, this);
    if (!l.IsInitializedAndIsLeader()) {
      VLOG(2) << "No longer the leader, skipping catalog versions task";
      pg_catalog_versions_bg_task_running_ = false;
      ResetCachedCatalogVersions();
      return;
    }
  }

  // Refresh the catalog versions in memory.
  VLOG(2) << "Running " << __func__ << " task";
  DbOidToCatalogVersionMap versions;
  Status s = GetYsqlAllDBCatalogVersionsImpl(&versions);
  if (!s.ok()) {
    LOG(WARNING) << "Catalog versions refresh task failed: " << s.ToString();
    ResetCachedCatalogVersions();
  } else {
    VLOG_WITH_FUNC(2) << "Refreshed " << versions.size() << " catalog versions in memory";
    const auto fingerprint = yb::FingerprintCatalogVersions<DbOidToCatalogVersionMap>(versions);
    VLOG_WITH_FUNC(2) << "fingerprint: " << fingerprint;
    LockGuard lock(heartbeat_pg_catalog_versions_cache_mutex_);
    if (heartbeat_pg_catalog_versions_cache_) {
      heartbeat_pg_catalog_versions_cache_->swap(versions);
    } else {
      heartbeat_pg_catalog_versions_cache_ = std::move(versions);
    }
    heartbeat_pg_catalog_versions_cache_fingerprint_ = fingerprint;
  }
  ScheduleRefreshPgCatalogVersionsTask();
}

Result<TabletDeleteRetainerInfo> CatalogManager::GetDeleteRetainerInfoForTabletDrop(
    const TabletInfo& tablet_info) {
  TabletDeleteRetainerInfo retainer;
  RETURN_NOT_OK(master_->snapshot_coordinator().PopulateDeleteRetainerInfoForTabletDrop(
      tablet_info, retainer));

  xcluster_manager_->PopulateTabletDeleteRetainerInfoForTabletDrop(tablet_info, retainer);

  {
    SharedLock lock(mutex_);
    CDCSDKPopulateDeleteRetainerInfoForTabletDrop(tablet_info, retainer);
  }

  return retainer;
}

Result<TabletDeleteRetainerInfo> CatalogManager::GetDeleteRetainerInfoForTableDrop(
    const TableInfo& table_info, const SnapshotSchedulesToObjectIdsMap& schedules_to_tables_map) {
  TabletDeleteRetainerInfo retainer;
  auto tablets_to_check = VERIFY_RESULT(table_info.GetTablets(IncludeInactive::kFalse));
  RETURN_NOT_OK(master_->snapshot_coordinator().PopulateDeleteRetainerInfoForTableDrop(
      table_info, tablets_to_check, schedules_to_tables_map, retainer));

  xcluster_manager_->PopulateTabletDeleteRetainerInfoForTableDrop(table_info, retainer);

  // xCluster and CDCSDK do not retain dropped tables.

  return retainer;
}

void CatalogManager::MarkTabletAsHidden(
    SysTabletsEntryPB& tablet_pb, const HybridTime& hide_ht,
    const TabletDeleteRetainerInfo& delete_retainer) const {
  // Update the hide time only if there isn't already a hide time.
  // During a tablet split, a parent tablet may already have been marked hidden as a result of
  // deactivating the parent tablet. In such cases the tablet may already have a hide time which
  // should not be updated on any subsequent operation such as Drop table.
  // Note that, PITR may bring back a parent tablet, but it does that by rewriting the
  // SysTabletsEntryPB from the past which will have the hide_hybrid_time not set.
  if (!tablet_pb.has_hide_hybrid_time()) {
    tablet_pb.set_hide_hybrid_time(hide_ht.ToUint64());
  }
  const auto retained_by_snapshot_schedules = delete_retainer.RetainedBySnapshotSchedules();
  if (!retained_by_snapshot_schedules.empty()) {
    *tablet_pb.mutable_retained_by_snapshot_schedules() = retained_by_snapshot_schedules;
  }
}

void CatalogManager::RecordHiddenTablets(
    const TabletInfos& new_hidden_tablets, const TabletDeleteRetainerInfo& delete_retainer) {
  if (new_hidden_tablets.empty()) {
    return;
  }

  // Update hidden_tablets_ first.
  {
    LockGuard lock(mutex_);
    hidden_tablets_.insert(
        hidden_tablets_.end(), new_hidden_tablets.begin(), new_hidden_tablets.end());

    RecordCDCSDKHiddenTablets(new_hidden_tablets, delete_retainer);
    xcluster_manager_->RecordHiddenTablets(new_hidden_tablets, delete_retainer);
  }

}

bool CatalogManager::ShouldRetainHiddenTablet(
    const TabletInfo& tablet, const ScheduleMinRestoreTime& schedule_to_min_restore_time) {
  const auto& tablet_id = tablet.tablet_id();
  if (master_->snapshot_coordinator().ShouldRetainHiddenTablet(
        tablet, schedule_to_min_restore_time)) {
    VLOG(1) << Format("Tablet $0 retained by snapshots", tablet_id);
    return true;
  }
  if (xcluster_manager_->ShouldRetainHiddenTablet(tablet)) {
    VLOG(1) << Format("Tablet $0 retained by xcluster", tablet_id);
    return true;
  }
  if (CDCSDKShouldRetainHiddenTablet(tablet_id)) {
    VLOG(1) << Format("Tablet $0 retained by CDCSDK", tablet_id);
    return true;
  }

  return false;
}

std::optional<UniverseUuid> CatalogManager::GetUniverseUuidIfExists() const {
  auto l = ClusterConfig()->LockForRead();
  if (!l->pb.has_universe_uuid()) {
    return std::nullopt;
  }
  auto universe_uuid_res = UniverseUuid::FromString(l->pb.universe_uuid());
  if (!universe_uuid_res.ok()) {
    LOG(WARNING) << "Failed to parse universe UUID: " << universe_uuid_res.status();
    return std::nullopt;
  }

  return *universe_uuid_res;
}

Result<std::vector<CatalogManager::StatefulServiceStatus>>
CatalogManager::GetStatefulServicesStatus() const {
  std::vector<CatalogManager::StatefulServiceStatus> result;
  std::vector<TableInfoPtr> tables_hosting_services;

  {
    SharedLock lock(mutex_);
    for (const auto& table_info : tables_->GetAllTables()) {
      if (!table_info->GetHostedStatefulServices().empty()) {
        tables_hosting_services.emplace_back(table_info);
      }
    }
  }

  for (const auto& table_info : tables_hosting_services) {
    const auto services = table_info->GetHostedStatefulServices();
    if (services.empty()) {
      continue;
    }
    for (const auto& service_kind : services) {
      CatalogManager::StatefulServiceStatus service_status;
      RSTATUS_DCHECK(
          StatefulServiceKind_IsValid(service_kind), IllegalState,
          Format("Unknown service kind $0", service_kind));
      service_status.service_name = StatefulServiceKind_Name((StatefulServiceKind)service_kind);

      service_status.service_table_id = table_info->id();
      auto tablets = VERIFY_RESULT(table_info->GetTablets());
      if (!tablets.empty()) {
        const auto tablet = tablets.front();
        service_status.service_tablet_id = tablet->tablet_id();
        auto leader_result = tablet->GetLeader();
        if (leader_result.ok()) {
          service_status.hosting_node = leader_result.get();
        } else {
          VLOG(1) << Format(
              "Failed to get leader for Stateful Service $0: $1", service_status.service_name,
              leader_result.status());
        }
      }

      result.emplace_back(std::move(service_status));
    }
  }

  return result;
}

InitialSysCatalogSnapshotWriter& CatalogManager::AllocateAndGetInitialSysCatalogSnapshotWriter() {
  initial_snapshot_writer_.emplace();
  return *initial_snapshot_writer_;
}

Result<std::vector<SysCatalogEntryDumpPB>> CatalogManager::FetchFromSysCatalog(
    SysRowEntryType type, const std::string& item_id_filter) {
  SCHECK_NOTNULL(sys_catalog_);
  SCHECK_NOTNULL(tablet_peer());
  auto tablet = VERIFY_RESULT(tablet_peer()->shared_tablet_safe());

  std::vector<SysCatalogEntryDumpPB> result;

  RETURN_NOT_OK(EnumerateSysCatalog(
      tablet.get(), schema(), type,
      [&item_id_filter, &result, &type](const Slice& id, const Slice& data) -> Status {
        if (!item_id_filter.empty() && item_id_filter != id.ToString()) {
          return Status::OK();
        }

        auto entry_pb = VERIFY_RESULT(SliceToCatalogEntityPB(type, data));
        auto& entry = result.emplace_back();
        entry.set_entry_type(type);
        entry.set_entity_id(id.ToString());
        entry.set_pb_debug_string(entry_pb->DebugString());

        return Status::OK();
      }));

  return result;
}

Status CatalogManager::WriteToSysCatalog(
    SysRowEntryType type, const std::string& item_id, const std::string& debug_string,
    QLWriteRequestPB::QLStmtType op_type) {
  auto pb = VERIFY_RESULT(DebugStringToCatalogEntityPB(type, debug_string));
  LOG_WITH_FUNC(WARNING) << "Updating Sys Catalog. OpType: "
                         << QLWriteRequestPB::QLStmtType_Name(op_type)
                         << ", EntryType: " << SysRowEntryType_Name(type)
                         << ", EntryId: " << item_id << ", EntryData: " << pb->DebugString();

  SCHECK_NOTNULL(sys_catalog_);

  // We need to lookup the consensus object for the current term because the leader_ready_term_
  // field is only set upon sys catalog load. We cannot use LeaderEpoch either as that is set
  // through the scoped leader shared lock which we cannot when in emergency_repair_mode.
  auto consensus = VERIFY_RESULT(tablet_peer()->GetConsensus());
  consensus::ConsensusStatePB consensus_state;
  RETURN_NOT_OK(GetCurrentConfig(&consensus_state));

  return sys_catalog_->ForceWrite(
      type, item_id, *pb.get(), op_type, consensus_state.current_term());
}

Status CatalogManager::DumpSysCatalogEntries(
    const DumpSysCatalogEntriesRequestPB* req, DumpSysCatalogEntriesResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG_WITH_FUNC(INFO) << req->ShortDebugString() << ", from: " << RequestorString(rpc);
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, entry_type);

  auto entries = VERIFY_RESULT(FetchFromSysCatalog(req->entry_type(), req->entity_id_filter()));
  for (auto& entry : entries) {
    *resp->add_entries() = std::move(entry);
  }

  return Status::OK();
}

Status CatalogManager::WriteSysCatalogEntry(
    const WriteSysCatalogEntryRequestPB* req, WriteSysCatalogEntryResponsePB* resp,
    rpc::RpcContext* rpc) {
  LOG_WITH_FUNC(INFO) << req->ShortDebugString() << ", from: " << RequestorString(rpc);
  SCHECK_PB_FIELDS_NOT_EMPTY(*req, entry_type);
  SCHECK_PB_FIELDS_SET(*req, op_type);
  SCHECK(
      FLAGS_emergency_repair_mode, IllegalState,
      "Updating sys_catalog is only allowed in emergency repair mode");

  auto statement_type = VERIFY_RESULT(ToQLStmtType(req->op_type()));
  return WriteToSysCatalog(
      req->entry_type(), req->entity_id(), req->pb_debug_string(), statement_type);
}

Result<TablegroupId> CatalogManager::GetTablegroupId(const TableId& table_id) {
  SharedLock lock(mutex_);
  const auto* tablegroup = tablegroup_manager_->FindByTable(table_id);
  SCHECK_FORMAT(tablegroup, NotFound, "No tablegroup found for table: $0", table_id);
  return tablegroup->id();
}

Result<TSDescriptorPtr> CatalogManager::GetClosestLiveTserver(bool* local_ts) const {
  if (local_ts) {
    *local_ts = false;
  }

  ServerRegistrationPB local_registration;
  RETURN_NOT_OK(master_->GetMasterRegistration(&local_registration));

  std::unordered_set<std::string> local_hosts;
  for (const auto& addr : local_registration.private_rpc_addresses()) {
    local_hosts.insert(addr.host());
  }

  const auto& local_cloud_info = local_registration.cloud_info();

  TSDescriptorVector descs;
  master_->ts_manager()->GetAllLiveDescriptorsInCluster(&descs, VERIFY_RESULT(placement_uuid()));

  auto best_score = CatalogManagerUtil::CloudInfoSimilarity::NO_MATCH;
  TSDescriptorPtr best_tserver;
  for (const auto& desc : descs) {
    const auto& ts_info = desc->GetTSInformationPB();
    DCHECK(ts_info.has_registration());
    if (!ts_info.has_registration()) {
      continue;
    }

    const auto& ts_cloud_info = ts_info.registration().common().cloud_info();
    auto ts_score = CatalogManagerUtil::ComputeCloudInfoSimilarity(ts_cloud_info, local_cloud_info);
    if (ts_score < best_score) {
      continue;
    }

    if (ts_score == CatalogManagerUtil::CloudInfoSimilarity::ZONE_MATCH) {
      // If this tserver is on the same node as master pick it.
      for (const auto& addr : ts_info.registration().common().private_rpc_addresses()) {
        if (local_hosts.contains(addr.host())) {
          if (local_ts) {
            *local_ts = true;
          }
          return desc;
        }
      }
    }

    best_score = ts_score;
    best_tserver = desc;
  }

  SCHECK(best_tserver, NotFound, "Couldn't find a live tablet server to connect to");

  return best_tserver;
}

bool CatalogManager::IsYsqlMajorCatalogUpgradeInProgress() const {
  return ysql_manager_->IsYsqlMajorCatalogUpgradeInProgress();
}

bool CatalogManager::SkipCatalogVersionChecks() {
  // Only skip if we are leader and the major catalog upgrade is in progress.
  SCOPED_LEADER_SHARED_LOCK(l, this);
  if (l.IsInitializedAndIsLeader()) {
    return IsYsqlMajorCatalogUpgradeInProgress();
  }
  return false;
}

}  // namespace master
}  // namespace yb
