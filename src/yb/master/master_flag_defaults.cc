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

#include "yb/master/master_flag_defaults.h"

#include <string>

#include "yb/master/master_defaults.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/size_literals.h"

DECLARE_bool(durable_wal_write);
DECLARE_bool(evict_failed_followers);
DECLARE_int32(follower_unavailable_considered_failed_sec);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_string(metric_node_name);
DECLARE_bool(quick_leader_election_on_create);
DECLARE_int32(remote_bootstrap_max_chunk_size);
DECLARE_string(rpc_bind_addresses);
DECLARE_bool(use_docdb_aware_bloom_filter);
DECLARE_int32(webserver_port);

namespace yb::master {

void SetMasterFlagDefaults() {
  FLAGS_rpc_bind_addresses = Format("0.0.0.0:$0", kMasterDefaultPort);
  FLAGS_webserver_port = kMasterDefaultWebPort;
  // Hotfix for https://github.com/yugabyte/yugabyte-db/issues/8731.
  // Before enabling bloom filters for the master tablet we need to check whether master code use
  // bloom filters properly at all, including if filter key is set correctly during scans.
  FLAGS_use_docdb_aware_bloom_filter = false;

  std::string host_name;
  if (GetHostname(&host_name).ok()) {
    FLAGS_metric_node_name = Format("$0:$1", host_name, kMasterDefaultWebPort);
  } else {
    LOG(INFO) << "Failed to get master's host name, keeping default metric_node_name";
  }

  // For masters we always want to fsync the WAL files (except in testing).
  FLAGS_durable_wal_write = true;
  // The masters of a new universe start together, so the sys catalog tablet does not need to wait
  // a full election timeout before electing its first leader.
  FLAGS_quick_leader_election_on_create = true;
  // Master has a lot less memory and relatively less data. So by default, let's keep the
  // RBS chunk size small.
  FLAGS_remote_bootstrap_max_chunk_size = 1_MB;

  // A multi-node Master leader should not evict failed Master followers
  // because there is no-one to assign replacement servers in order to maintain
  // the desired replication factor. (It's not turtles all the way down!)
  FLAGS_evict_failed_followers = false;

  FLAGS_follower_unavailable_considered_failed_sec = 2 * MonoTime::kSecondsPerHour;
  FLAGS_log_min_seconds_to_retain = 2 * MonoTime::kSecondsPerHour;
}

}  // namespace yb::master
