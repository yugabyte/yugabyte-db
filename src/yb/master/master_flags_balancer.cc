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

// Master gflags for the load balancer and tablet splitting. The Rust master reads them by name, or
// accepts them without honoring them yet (see master_flags_baseline.txt). Their names, types,
// defaults, help text, tags and AutoFlag classes are part of the yb-master command line and of
// auto_flags.json: change them only as you would change any released flag.

#include "yb/util/flags.h"

DEFINE_RUNTIME_bool(enable_load_balancing, true,
    "Choose whether to enable cluster load balancing.");

DEFINE_RUNTIME_bool(transaction_tables_use_preferred_zones, false,
    "Choose whether transaction tablet leaders respect preferred zones.");

DEFINE_RUNTIME_bool(enable_global_load_balancing, true,
    "Choose whether to allow the cluster balancer to make moves that strictly only balance "
    "global load. Note that global balancing only occurs after all tables are balanced.");

DEFINE_RUNTIME_int32(leader_balance_threshold, 0,
    "Number of leaders per each tablet server per table to balance below. If this is configured to "
    "0 (the default), the leaders will be balanced optimally at extra cost.");
TAG_FLAG(leader_balance_threshold, advanced);

DEFINE_RUNTIME_int32(leader_balance_unresponsive_timeout_ms, 3 * 1000,
    "The period of time that a master can go without receiving a heartbeat from a "
    "tablet server before considering it unresponsive. Unresponsive servers are "
    "excluded from leader balancing.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_tablet_remote_bootstraps, -1,
    "Maximum number of tablets being remote bootstrapped across the cluster. If set to -1, there "
    "is no global limit on the number of concurrent remote bootstraps (per-table or per-tserver "
    "limits still apply).");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_tablet_remote_bootstraps_per_table, -1,
    "Maximum number of tablets being remote bootstrapped for any table. The maximum "
    "number of remote bootstraps across the cluster is still limited by the flag "
    "load_balancer_max_concurrent_tablet_remote_bootstraps. This flag is meant to prevent "
    "a single table from using all the available remote bootstrap sessions and starving other "
    "tables.");

DEFINE_RUNTIME_int32(load_balancer_max_inbound_remote_bootstraps_per_tserver, 50,
    "Maximum number of tablets simultaneously remote bootstrapping on a tserver. Past this value, "
    "the cluster balancer will not add tablets to this tserver.");

DEFINE_RUNTIME_int32(load_balancer_min_inbound_remote_bootstraps_per_tserver, 4,
    "Minimum number of tablets simultaneously remote bootstrapping on a tserver (if there are this "
    "many to remote bootstrap). This forces some parallelism during remote bootstrap, which avoids "
    "single-threaded bottlenecks.");

DEFINE_RUNTIME_uint32(load_balancer_min_free_disk_space_pct, 5,
    "Reject a tablet server as the destination for a tablet replica if hosting that replica would "
    "leave less than this percentage of its total disk capacity free. Disk usage is estimated from "
    "the usage the tablet server reported in its last heartbeat plus the full size of every "
    "replica it is already starting. Tablet servers that have not reported their disk capacity are "
    "never rejected. Defaults to the same threshold as --reject_writes_min_disk_space_pct, past "
    "which a tablet server rejects writes anyway. Set to 0 to disable the check.");

DEFINE_RUNTIME_int32(load_balancer_max_over_replicated_tablets, 50,
    "Maximum number of running tablet replicas per table that are allowed to be over the "
    "configured replication factor. This controls the amount of space amplification in the cluster "
    "when tablet removal is slow. Adds to tablets below the replication factor are not limited. "
    "A value less than 0 means no limit.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_adds, 25,
    "Maximum number of tablet peer replicas to add in any one run of the cluster balancer.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_removals, 50,
    "Maximum number of over-replicated tablet peer removals to do in any one run of the "
    "cluster balancer. A value less than 0 means no limit.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_moves, 100,
    "Maximum number of tablet leaders on tablet servers (across the cluster) to move in "
    "any one run of the cluster balancer.");

DEFINE_RUNTIME_int32(load_balancer_max_concurrent_moves_per_table, -1,
    "Maximum number of tablet leaders per table to move in any one run of the cluster "
    "balancer. The maximum number of tablet leader moves across the cluster is still "
    "limited by the flag load_balancer_max_concurrent_moves. This flag is meant to "
    "prevent a single table from using all of the leader moves quota and starving "
    "other tables."
    "If set to -1, the number of leader moves per table is set to the global number of leader "
    "moves (load_balancer_max_concurrent_moves).");

DEFINE_RUNTIME_int32(load_balancer_num_idle_runs, 5,
    "Number of idle runs of cluster balancer to deem it idle.");

DEFINE_test_flag(bool, load_balancer_handle_under_replicated_tablets_only, false,
                 "Limit the functionality of the cluster balancer during tests so tests can make "
                 "progress");

// No longer used because leader stepdown is not as slow as it used to be.
DEPRECATE_FLAG(bool, load_balancer_skip_leader_as_remove_victim, "10_2022");

DEFINE_RUNTIME_bool(allow_leader_balancing_dead_node, true,
    "When a tserver is marked as dead, do we continue leader balancing for tables that "
    "have a replica on this tserver");

DEFINE_test_flag(int32, load_balancer_wait_ms, 0,
                 "For testing purposes, number of milliseconds to wait at the start of a cluster "
                 "balancer iteration.");

DEPRECATE_FLAG(bool, load_balancer_count_move_as_add, "03_2025");

DEFINE_RUNTIME_bool(load_balancer_drive_aware, true,
    "When LB decides to move a tablet from server A to B, on the target LB "
    "should select the tablet to move from most loaded drive.");

DEFINE_RUNTIME_bool(load_balancer_ignore_cloud_info_similarity, false,
    "If true, ignore the similarity between cloud infos when deciding which tablet to move");

DEFINE_RUNTIME_bool(cluster_balancer_stepdown_to_preferred_leader_on_remove, true,
    "If true, when removing a replica which happens to be the leader from a tablet, the cluster "
    "balancer will step down the leader to a tserver in the most preferred zone.");

DEFINE_RUNTIME_int32(process_split_tablet_candidates_interval_msec, 0,
    "The minimum time between automatic splitting attempts. The actual splitting time "
    "between runs is also affected by catalog_manager_bg_task_wait_ms, which controls how "
    "long the bg tasks thread sleeps at the end of each loop. The top-level automatic "
    "tablet splitting method, which checks for the time since last run, is run once per "
    "loop.");

DEPRECATE_FLAG(int32, max_queued_split_candidates, "10_2022");

DEFINE_RUNTIME_uint64(outstanding_tablet_split_limit, 0,
    "Limit of the number of outstanding tablet splits. Limitation is disabled if this "
    "value is set to 0.");

DEFINE_RUNTIME_int64(outstanding_tablet_split_limit_per_tserver, -1,
    "Limit of the number of outstanding tablet splits per node. Limitation is disabled "
    "if this value is set to 0. If the value is negative (-1 by default), the limit is "
    "derived from the CPU count: 1 for nodes with up to 4 cores, 2 otherwise.");

DEFINE_RUNTIME_bool(enable_tablet_split_of_pitr_tables, true,
    "When set, it enables automatic tablet splitting of tables covered by "
    "Point In Time Restore schedules.");

DEFINE_RUNTIME_bool(enable_tablet_split_of_uncommitted_ysql_tables, false,
    "When set, it enables automatic tablet splitting of YSQL tables whose creating DDL "
    "transaction has not committed yet.");
TAG_FLAG(enable_tablet_split_of_uncommitted_ysql_tables, advanced);

DEFINE_RUNTIME_AUTO_bool(enable_tablet_split_of_tables_with_vector_index, kExternal, false, true,
    "When set, it enables automatic tablet splitting for tables with vector indexes");

DEFINE_RUNTIME_uint64(tablet_split_limit_per_table, 0,
    "Limit of the number of tablets per table for tablet splitting. Limitation is "
    "disabled if this value is set to 0.");

DEFINE_RUNTIME_uint64(prevent_split_for_ttl_tables_for_seconds, 86400,
    "Seconds between checks for whether to split a table with TTL. Checks are disabled "
    "if this value is set to 0.");

DEFINE_RUNTIME_uint64(prevent_split_for_small_key_range_tablets_for_seconds, 300,
    "Seconds between checks for whether to split a tablet whose key range is too small "
    "to be split. Checks are disabled if this value is set to 0.");

DEFINE_RUNTIME_uint64(tablet_split_rejected_candidates_log_interval_secs, 900,
    "Minimum seconds between summarized logs of tablets/tables rejected as automatic split "
    "candidates. Set to 0 to log a summary on every automatic tablet splitting pass.");

DEFINE_RUNTIME_bool(sort_automatic_tablet_splitting_candidates, true,
    "Whether we should sort candidates for new automatic tablet splits, so that low phase takes "
    "precedence over high and final phases, and largest candidates are prioritized within each "
    "phase.");

DEFINE_RUNTIME_double(tablet_split_min_size_ratio, 0.8,
    "If sorting by size is enabled, a tablet will only be considered for splitting if the ratio "
    "of its size to the largest split candidate of its table is at least this value. "
    "Valid flag values are 0 to 1 (inclusive). "
    "Setting this to 0 means any tablet that does not exceed tserver / global limits can be split. "
    "Setting this to 1 forces the tablet splitting algorithm to always split the largest candidate "
    "of each table (even if that means waiting for existing splits to complete).");

DEFINE_test_flag(bool, skip_partitioning_version_validation, false,
    "When set, skips partitioning_version checks to prevent tablet splitting.");

DEFINE_RUNTIME_int32(inflight_splits_completion_timeout_secs, 600,
    "Total time to wait for all inflight splits to complete during Restore.");
TAG_FLAG(inflight_splits_completion_timeout_secs, advanced);

DEFINE_RUNTIME_int32(pitr_max_restore_duration_secs, 600,
    "Maximum amount of time to complete a PITR restore.");
TAG_FLAG(pitr_max_restore_duration_secs, advanced);

DEFINE_RUNTIME_int32(pitr_split_disable_check_freq_ms, 500,
    "Delay before retrying to see if inflight tablet split operations have completed "
    "after which PITR restore can be performed.");
TAG_FLAG(pitr_split_disable_check_freq_ms, advanced);

DEFINE_RUNTIME_bool(split_respects_tablet_replica_limits, false,
    "Whether to check the universe tablet replica limit before splitting a tablet. When this flag "
    "and enforce_tablet_replica_limits are both true, the system will no longer split tablets when "
    "the limit machinery determines the universe cannot support any more tablet replicas.");
TAG_FLAG(split_respects_tablet_replica_limits, advanced);

DEFINE_UNKNOWN_double(balancer_load_max_standard_deviation, 2.0,
    "The standard deviation among the tserver load, above which that distribution "
    "is considered not balanced.");
TAG_FLAG(balancer_load_max_standard_deviation, advanced);
