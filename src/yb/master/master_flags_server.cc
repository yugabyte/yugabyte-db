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

// Master gflags for the master server: services, heartbeats, tserver registration and leader locks.
// The Rust master reads them by name, or accepts them without honoring them yet (see
// master_flags_baseline.txt). Their names, types, defaults, help text, tags and AutoFlag classes
// are part of the yb-master command line and of auto_flags.json: change them only as you would
// change any released flag.

#include "yb/master/master_defaults.h"
#include "yb/util/flags.h"
#include "yb/util/tsan_util.h"

constexpr int32_t kMasterLogLockWarningMsDefault =
    ::yb::RegularBuildVsSanitizers<int32_t>(1000, 3000);

constexpr int32_t kMasterLeaderLockStackTraceMsDefault =
    ::yb::RegularBuildVsSanitizers<int32_t>(3000, 9000);

DEFINE_test_flag(int32, timeout_non_leader_master_rpcs_ms, 0,
    "Timeout all master requests to non leader for the specified number of milliseconds");

DEFINE_NON_RUNTIME_int32(master_rpc_timeout_ms, 1500,
             "Timeout for retrieving master registration over RPC.");

DEFINE_UNKNOWN_int32(master_yb_client_default_timeout_ms, 60000,
             "Default timeout for the YBClient embedded into the master.");

DEFINE_NON_RUNTIME_int32(master_backup_svc_queue_length, 50,
             "RPC queue length for master backup service");
TAG_FLAG(master_backup_svc_queue_length, advanced);

DEPRECATE_FLAG(int32, master_tserver_svc_num_threads, "02_2024");

DEPRECATE_FLAG(int32, master_svc_num_threads, "02_2024");

DEPRECATE_FLAG(int32, master_consensus_svc_num_threads, "02_2024");

DEPRECATE_FLAG(int32, master_remote_bootstrap_svc_num_threads, "02_2024");

DEFINE_NON_RUNTIME_int32(master_tserver_svc_queue_length, 1000,
             "RPC queue length for master tserver service");
TAG_FLAG(master_tserver_svc_queue_length, advanced);

DEFINE_NON_RUNTIME_int32(master_svc_queue_length, 1000,
             "RPC queue length for master service");
TAG_FLAG(master_svc_queue_length, advanced);

DEFINE_NON_RUNTIME_int32(master_ysql_lease_svc_queue_length, 1000,
             "RPC queue length for master YSQL lease service");
TAG_FLAG(master_ysql_lease_svc_queue_length, advanced);

DEFINE_NON_RUNTIME_int32(master_consensus_svc_queue_length, 1000,
             "RPC queue length for master consensus service");
TAG_FLAG(master_consensus_svc_queue_length, advanced);

DEFINE_NON_RUNTIME_int32(master_remote_bootstrap_svc_queue_length, 50,
             "RPC queue length for master remote bootstrap service");
TAG_FLAG(master_remote_bootstrap_svc_queue_length, advanced);

DEFINE_NON_RUNTIME_int32(master_xrepl_svc_queue_length, 50,
              "RPC queue length for the CDC service");
TAG_FLAG(master_xrepl_svc_queue_length, advanced);

DEFINE_UNKNOWN_int32(master_inject_latency_on_tablet_lookups_ms, 0,
             "Number of milliseconds that the master will sleep before responding to "
             "requests for tablet locations.");
TAG_FLAG(master_inject_latency_on_tablet_lookups_ms, unsafe);
TAG_FLAG(master_inject_latency_on_tablet_lookups_ms, hidden);

DEFINE_test_flag(bool, master_fail_transactional_tablet_lookups, false,
                 "Whether to fail all lookup requests to transactional table.");

DEFINE_RUNTIME_int32(blacklist_progress_initial_delay_secs, yb::master::kDelayAfterFailoverSecs,
    "When a master leader failsover, the time until which the progress of load movement "
    "off the blacklisted tservers is reported as 0. This initial delay "
    "gives sufficient time for heartbeats so that we don't report"
    " a premature incorrect completion.");

DEFINE_RUNTIME_bool(delay_leader_blacklist_completion_percent_until_tservers_heartbeat, true,
    "When set, the master will wait for all live tservers to heartbeat before reporting "
    "leader blacklist completion percent.");
TAG_FLAG(delay_leader_blacklist_completion_percent_until_tservers_heartbeat, advanced);

DEFINE_UNKNOWN_double(master_slow_get_registration_probability, 0,
              "Probability of injecting delay in GetMasterRegistration.");

DEFINE_RUNTIME_bool(master_list_raft_peers_check_is_leader, false,
    "When enabled, ListMasterRaftPeers RPC will reject "
    "with NOT_THE_LEADER if processed by a non-leader master."
);

DEFINE_UNKNOWN_int32(tablet_report_limit, 1000,
             "Max Number of tablets to report during a single heartbeat. "
             "If this is set to INT32_MAX, then heartbeat will report all dirty tablets.");
TAG_FLAG(tablet_report_limit, advanced);

DEFINE_test_flag(string, master_universe_uuid, "",
                 "When set, use this mocked uuid to compare against the universe_uuid "
                 "from the tserver request.");

DEFINE_RUNTIME_AUTO_bool(master_enable_universe_uuid_heartbeat_check, kLocalPersisted, false, true,
    "When true, enables a sanity check between masters and tservers to prevent tservers from "
    "mistakenly heartbeating to masters in different universes. Master leader will check the "
    "universe_uuid passed by tservers against with its own copy of universe_uuid "
    "and reject the request if there is a mismatch. Master sends its universe_uuid with the "
    "response.");
TAG_FLAG(master_enable_universe_uuid_heartbeat_check, advanced);

DEFINE_RUNTIME_AUTO_bool(use_tablet_report_pending_config_op_id, kLocalVolatile, false, true,
    "When true, allows master to rely on pending_config_op_id received within TServer->Master "
    "heartbeats.");

DEFINE_test_flag(bool, skip_processing_tablet_metadata, false,
                 "Whether to skip processing tablet metadata for TSHeartbeat.");

DEFINE_NON_RUNTIME_uint32(xcluster_guarded_lease_duration_ms, 2 * 60 * 1000,
    "Duration of xCluster-guarded information lease in milliseconds; not safe to lower.");
TAG_FLAG(xcluster_guarded_lease_duration_ms, advanced);

DEFINE_RUNTIME_int32(catalog_manager_report_batch_size, 1,
    "The max number of tablets evaluated in the heartbeat as a single SysCatalog update.");
TAG_FLAG(catalog_manager_report_batch_size, advanced);

DEFINE_RUNTIME_int32(master_ts_heartbeat_long_operation_warning_ms, 1000,
    "Log a warning (and dump the handler thread's stack) if processing a TSHeartbeat RPC on the "
    "master takes longer than this many ms.");

DEFINE_RUNTIME_bool(catalog_manager_wait_for_new_tablets_to_elect_leader, true,
    "Whether the catalog manager should wait for a newly created tablet to "
    "elect a leader before considering it successfully created. "
    "This is disabled in some tests where we explicitly manage leader "
    "election.");
TAG_FLAG(catalog_manager_wait_for_new_tablets_to_elect_leader, hidden);

DEFINE_RUNTIME_double(heartbeat_safe_deadline_ratio, .20,
    "When the heartbeat deadline has this percentage of time remaining, "
    "the master should halt tablet report processing so it can respond in time.");

DEFINE_RUNTIME_bool(master_enable_deletion_check_for_orphaned_tablets, true,
    "When set, this flag adds stricter protection around the deletion of orphaned tablets. When "
    "master leader is processing a tablet report and doesn't know about a tablet, explicitly "
    "check that the tablet has been deleted in the past. If it has, then issue a DeleteTablet "
    "to the tservers. Otherwise, it means that tserver has heartbeated to the wrong cluster, "
    "or there has been sys catalog corruption. In this case, log an error but don't actually "
    "delete any data.");

// Temporary.  Can be removed after long-run testing.
// TODO: how temporary is this?
DEFINE_RUNTIME_bool(master_ignore_stale_cstate, true,
    "Whether Master processes the raft config when the version is lower.");
TAG_FLAG(master_ignore_stale_cstate, hidden);

DEFINE_RUNTIME_bool(master_tombstone_evicted_tablet_replicas, true,
    "Whether the Master should tombstone (delete) tablet replicas that "
    "are no longer part of the latest reported raft config.");
TAG_FLAG(master_tombstone_evicted_tablet_replicas, hidden);

DEFINE_RUNTIME_uint32(maximum_tablet_leader_lease_expired_secs, 2 * 60,
    "If the leader lease in master's view has expired for this amount of seconds, "
    "treat the lease as expired for too long time.");

DEFINE_RUNTIME_bool(use_create_table_leader_hint, true,
    "Whether the Master should hint which replica for each tablet should "
    "be leader initially on tablet creation.");

DEFINE_RUNTIME_bool(send_leader_blacklisted_tservers_on_heartbeat, true,
    "When set, the master will send the list of leader-blacklisted tservers with no leaders "
    "on the heartbeat response.");
TAG_FLAG(send_leader_blacklisted_tservers_on_heartbeat, advanced);

DEFINE_test_flag(uint64, inject_latency_during_tablet_report_ms, 0,
                 "Number of milliseconds to sleep during the processing of a tablet batch.");

DEFINE_test_flag(bool, simulate_sys_catalog_data_loss, false,
    "On the heartbeat processing path, simulate a scenario where tablet metadata is missing due to "
    "a corruption. ");

DEFINE_UNKNOWN_int32(master_log_lock_warning_ms, kMasterLogLockWarningMsDefault,
             "Print warnings if the master leader shared lock is held for longer than this amount "
             "of time. Note that this is a shared lock, so these warnings simply indicate "
             "long-running master operations that could delay system catalog loading by a new "
             "master leader.");

DEFINE_UNKNOWN_int32(master_leader_lock_stack_trace_ms, kMasterLeaderLockStackTraceMsDefault,
             "Dump a stack trace if the master leader shared lock is held for longer than this "
             "of time. Also see master_log_lock_warning_ms.");

DEFINE_RUNTIME_int32(are_nodes_safe_to_take_down_timeout_buffer_ms, 2000, "How much earlier than "
    "the deadline to return from AreNodesSafeToTakeDown. This allows the caller to receive a "
    "more readable error instead of a generic timeout error.");

DEFINE_NON_RUNTIME_bool(master_register_ts_check_desired_host_port, true,
    "When set to true, master will only do duplicate address checks on the used host/port instead "
    "of on all. The used host/port combination depends on the value of --use_private_ip.");
TAG_FLAG(master_register_ts_check_desired_host_port, advanced);

DEFINE_RUNTIME_int32(tserver_unresponsive_timeout_ms, 60 * 1000,
    "The period of time that a Master can go without receiving a heartbeat from a tablet server "
    "before considering it unresponsive. Unresponsive servers are not selected when assigning "
    "replicas during table creation or re-replication.");
TAG_FLAG(tserver_unresponsive_timeout_ms, advanced);

DEFINE_RUNTIME_AUTO_bool(persist_tserver_registry, kLocalPersisted, false, true,
    "Whether to persist the map of registered tservers in the universe to the sys catalog. Also "
    "controls whether to reload the map of registered tservers from the sys catalog when reloading "
    "the sys catalog.");

DEFINE_RUNTIME_bool(skip_tserver_version_checks, false, "Skip all tserver version checks");

DEFINE_RUNTIME_int32(hide_dead_node_threshold_mins, 60 * 24,
    "After this many minutes of no heartbeat from a node, hide it from the UI "
    "(we presume it has been removed from the cluster). If -1, this flag is ignored and node is "
    "never hidden from the UI");

DEFINE_test_flag(bool, master_ui_redirect_to_leader, true,
                 "Redirect master UI requests to the master leader");

DEFINE_RUNTIME_uint32(leaderless_tablet_alert_delay_secs, 2 * 60,
    "From master's view, if the tablet doesn't have a valid leader for this amount of seconds, "
    "alert it as a leaderless tablet.");

DEFINE_test_flag(int32, sleep_before_reporting_lb_ui_ms, 0,
                 "Sleep before reporting tasks in the cluster balancer UI, to give tasks a chance "
                 "to complete.");

DEPRECATE_FLAG(uint64, master_maximum_heartbeats_without_lease, "12_2023");
