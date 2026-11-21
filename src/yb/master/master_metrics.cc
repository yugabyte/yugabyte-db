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

// Metrics the master host (src/yb/master_host) instantiates, and prototypes tests look up a
// master's metrics by. The Rust master's own metrics are created by the metrics bridge
// (src/yb/rust_bridge/metrics_bridge.cc).

#include "yb/util/metrics.h"

METRIC_DEFINE_entity(cluster);

METRIC_DEFINE_gauge_int64(cluster,
                          is_load_balancing_enabled,
                          "Is Cluster Balancing Enabled",
                          yb::MetricUnit::kUnits,
                          "Is cluster balancing enabled in the cluster where "
                          "1 indicates it is enabled.");

METRIC_DEFINE_gauge_uint32(cluster,
                           tablets_in_wrong_placement,
                           "Tablets in Wrong/Blacklisted Placement",
                           yb::MetricUnit::kUnits,
                           "Number of tablet peers in invalid or blacklisted locations.");

METRIC_DEFINE_gauge_uint32(cluster,
                           blacklisted_leaders,
                           "Blacklisted Leaders",
                           yb::MetricUnit::kUnits,
                           "Number of tablet leaders in locations from which leaders are "
                           "blacklisted.");

METRIC_DEFINE_gauge_uint32(cluster,
                           total_table_load_difference,
                           "Sum of Table Load Difference",
                           yb::MetricUnit::kUnits,
                           "The minimum number of replicas that need to be added / moved for the "
                           "cluster to be balanced.");

METRIC_DEFINE_gauge_uint64(cluster,
                           estimated_data_to_balance_bytes,
                           "Estimated Data to Balance",
                           yb::MetricUnit::kBytes,
                           "The approximate amount of data that needs to be moved to balance the "
                           "cluster. It is approximate because it is calculated as the sum across "
                           "all tables of the number of replicas that need to be added / moved for "
                           "the table to be balanced, multiplied by the average size of a tablet "
                           "in that table.");

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

METRIC_DEFINE_event_stats(
    server, load_balancer_duration, "Cluster balancer duration",
    yb::MetricUnit::kMilliseconds, "Duration of one cluster balancer run (in milliseconds)");

METRIC_DEFINE_counter(cluster, backfill_aborted,
    "Number of index backfills aborted on the master",
    yb::MetricUnit::kRequests,
    "Counts BackfillTable::Abort invocations on the master.");
