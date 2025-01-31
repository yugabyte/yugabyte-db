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

#include "yb/server/server_base_options.h"

#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "yb/common/common_net.pb.h"

#include "yb/gutil/strings/join.h"

#include "yb/master/master_defaults.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

DEFINE_UNKNOWN_string(placement_uuid, "",
              "The uuid of the tservers cluster/placement.");

DEFINE_UNKNOWN_int32(master_discovery_timeout_ms, 3600000,
             "Timeout for masters to discover each other during cluster creation/startup");
TAG_FLAG(master_discovery_timeout_ms, hidden);

DECLARE_bool(TEST_use_custom_varz);

DECLARE_string(placement_cloud);
DECLARE_string(placement_region);
DECLARE_string(placement_zone);

namespace yb {
namespace server {

using std::vector;
using namespace std::literals;

DEFINE_UNKNOWN_string(server_dump_info_path, "",
              "Path into which the server information will be "
              "dumped after startup. The dumped data is described by "
              "ServerStatusPB in server_base.proto. The dump format is "
              "determined by --server_dump_info_format");
DEFINE_UNKNOWN_string(server_dump_info_format, "json",
              "Format for --server_dump_info_path. This may be either "
              "'pb' or 'json'.");
TAG_FLAG(server_dump_info_path, hidden);
TAG_FLAG(server_dump_info_format, hidden);

DEFINE_UNKNOWN_int32(metrics_log_interval_ms, 0,
             "Interval (in milliseconds) at which the server will dump its "
             "metrics to a local log file. The log files are located in the same "
             "directory as specified by the -log_dir flag. If this is not a positive "
             "value, then metrics logging will be disabled.");
TAG_FLAG(metrics_log_interval_ms, advanced);

DEFINE_UNKNOWN_string(server_broadcast_addresses, "", "Broadcast addresses for this server.");

ServerBaseOptions::ServerBaseOptions(int default_port)
    : env(Env::Default()),
      dump_info_path(FLAGS_server_dump_info_path),
      dump_info_format(FLAGS_server_dump_info_format),
      metrics_log_interval_ms(FLAGS_metrics_log_interval_ms),
      placement_uuid(FLAGS_placement_uuid),
      server_broadcast_addresses(FLAGS_server_broadcast_addresses) {
  rpc_opts.default_port = default_port;
  if (!FLAGS_server_broadcast_addresses.empty()) {
    auto status = HostPort::ParseStrings(FLAGS_server_broadcast_addresses, default_port,
                                         &broadcast_addresses);
    LOG_IF(DFATAL, !status.ok()) << "Bad public IPs " << FLAGS_server_broadcast_addresses
                                 << ": " << status;
  }
}

ServerBaseOptions::ServerBaseOptions(const ServerBaseOptions& options)
    : env(options.env),
      server_type(options.server_type),
      fs_opts(options.fs_opts),
      rpc_opts(options.rpc_opts),
      webserver_opts(options.webserver_opts),
      dump_info_path(options.dump_info_path),
      dump_info_format(options.dump_info_format),
      metrics_log_interval_ms(options.metrics_log_interval_ms),
      placement_uuid(options.placement_uuid),
      master_addresses_flag(options.master_addresses_flag),
      server_broadcast_addresses(options.server_broadcast_addresses),
      broadcast_addresses(options.broadcast_addresses),
      placement_cloud_(options.placement_cloud_),
      placement_region_(options.placement_region_),
      placement_zone_(options.placement_zone_) {
  CompleteWebserverOptions();
  SetMasterAddressesNoValidation(options.GetMasterAddresses());
}

WebserverOptions& ServerBaseOptions::CompleteWebserverOptions() {
  if (webserver_opts.bind_interface.empty()) {
    std::vector<HostPort> bind_addresses;
    auto status = HostPort::ParseStrings(rpc_opts.rpc_bind_addresses, 0, &bind_addresses);
    LOG_IF(DFATAL, !status.ok()) << "Invalid rpc_bind_address "
                                 << rpc_opts.rpc_bind_addresses << ": " << status;
    if (!bind_addresses.empty()) {
      webserver_opts.bind_interface = bind_addresses.at(0).host();
    }
  }

  if (FLAGS_TEST_use_custom_varz) {
    if (!fs_opts.data_paths.empty() &&
        // Only add information about fs_data_dirs once even if called multiple times.
        webserver_opts.TEST_custom_varz.find("fs_data_dirs") == std::string::npos) {
      if (!webserver_opts.TEST_custom_varz.empty()) {
        webserver_opts.TEST_custom_varz += "\n";
      }
      webserver_opts.TEST_custom_varz += "fs_data_dirs\n" + JoinStrings(fs_opts.data_paths, ",");
    }
    LOG(INFO) << "custom_varz is "
              << std::regex_replace(webserver_opts.TEST_custom_varz, std::regex{"\n"}, " | ");
  }

  return webserver_opts;
}

std::string ServerBaseOptions::HostsString() const {
  return !server_broadcast_addresses.empty() ? server_broadcast_addresses
                                             : rpc_opts.rpc_bind_addresses;
}

void ServerBaseOptions::SetMasterAddressesNoValidation(MasterAddressesPtr master_addresses) {
  if (master_addresses) {
    LOG(INFO) << "Updating master addrs to " << MasterAddressesToString(*master_addresses);
  }

  std::lock_guard l(master_addresses_mtx_);
  master_addresses_ = master_addresses;
}

MasterAddressesPtr ServerBaseOptions::GetMasterAddresses() const {
  std::lock_guard l(master_addresses_mtx_);
  return master_addresses_;
}

template <class It>
Result<std::vector<HostPort>> MasterHostPortsFromIterators(It begin, const It& end) {
  std::vector<HostPort> result;
  for (;;) {
    auto split = std::find(begin, end, ',');
    result.push_back(VERIFY_RESULT(HostPort::FromString(
        std::string(begin, split), master::kMasterDefaultPort)));
    if (split == end) {
      break;
    }
    begin = ++split;
  }
  return result;
}

Result<MasterAddresses> ParseMasterAddresses(const std::string& source) {
  MasterAddresses result;
  auto token_begin = source.begin();
  auto end = source.end();
  for (auto i = source.begin(); i != end; ++i) {
    if (*i == '{') {
      if (token_begin != i) {
        return STATUS_FORMAT(InvalidArgument, "'{' in the middle of token in $0", source);
      }
      ++i;
      auto token_end = std::find(i, end, '}');
      if (token_end == end) {
        return STATUS_FORMAT(InvalidArgument, "'{' is not terminated in $0", source);
      }
      result.push_back(VERIFY_RESULT(MasterHostPortsFromIterators(i, token_end)));
      i = token_end;
      ++i;
      token_begin = i;
      if (i == end) {
        break;
      }
      if (*i != ',') {
        return STATUS_FORMAT(InvalidArgument, "',' expected after '}' in $0", source);
      }
      ++token_begin;
    } else if (*i == ',') {
      result.push_back(VERIFY_RESULT(MasterHostPortsFromIterators(token_begin, i)));
      token_begin = i;
      ++token_begin;
    }
  }
  if (token_begin != end) {
    result.push_back(VERIFY_RESULT(MasterHostPortsFromIterators(token_begin, end)));
  }
  return std::move(result);
}

Status DetermineMasterAddresses(
    const std::string& master_addresses_flag_name, const std::string& master_addresses_flag,
    uint64_t master_replication_factor, MasterAddresses* master_addresses,
    std::string* master_addresses_resolved_str) {
  const auto kResolvePeriod = 1s;

  *master_addresses = VERIFY_RESULT_PREPEND(
      ParseMasterAddresses(master_addresses_flag),
      Format("Couldn't parse the $0 flag ('$1')",
             master_addresses_flag_name, master_addresses_flag));

  if (master_replication_factor <= 0) {
    *master_addresses_resolved_str = master_addresses_flag;
    return Status::OK();
  }

  std::vector<Endpoint> addrs;
  for (;;) {
    addrs.clear();
    for (const auto& list : *master_addresses) {
      for (const auto& hp : list) {
        auto s = hp.ResolveAddresses(&addrs);
        LOG_IF(WARNING, !s.ok()) << s;
      }
    }
    if (addrs.size() >= master_replication_factor) {
      break;
    }
    std::this_thread::sleep_for(kResolvePeriod);
  }
  if (addrs.size() > master_replication_factor) {
    return STATUS_FORMAT(
        ConfigurationError, "Expected $0 master endpoints, but got: $1",
        master_replication_factor, yb::ToString(addrs));
  }
  LOG(INFO) << Format("Resolved master addresses: $0", yb::ToString(addrs));
  master_addresses->clear();
  std::vector<std::string> master_addr_strings(addrs.size());
  for (const auto& addr : addrs) {
    auto hp = HostPort(addr);
    master_addresses->emplace_back(std::vector<HostPort>(1, hp));
    master_addr_strings.emplace_back(hp.ToString());
  }
  *master_addresses_resolved_str = JoinStrings(master_addr_strings, ",");
  return Status::OK();
}

Result<std::vector<Endpoint>> ResolveMasterAddresses(const MasterAddresses& master_addresses) {
  const auto resolve_sleep_interval_sec = 1;
  auto resolve_max_iterations =
      (FLAGS_master_discovery_timeout_ms / 1000) / resolve_sleep_interval_sec;
  if (resolve_max_iterations < 120) {
    resolve_max_iterations = 120;
  }

  std::vector<Endpoint> result;
  for (const auto& list : master_addresses) {
    for (const auto& master_addr : list) {
      // Retry resolving master address for 'master_discovery_timeout' period of time
      int num_iters = 0;
      Status s = master_addr.ResolveAddresses(&result);
      while (!s.ok()) {
        num_iters++;
        if (num_iters > resolve_max_iterations) {
          return STATUS_FORMAT(ConfigurationError, "Couldn't resolve master service address '$0'",
              master_addr);
        }
        std::this_thread::sleep_for(std::chrono::seconds(resolve_sleep_interval_sec));
        s = master_addr.ResolveAddresses(&result);
      }
    }
  }
  return result;
}

std::string MasterAddressesToString(const MasterAddresses& addresses) {
  std::string result;
  bool first_master = true;
  for (const auto& list : addresses) {
    if (first_master) {
      first_master = false;
    } else {
      result += ',';
    }
    result += '{';
    bool first_address = true;
    for (const auto& hp : list) {
      if (first_address) {
        first_address = false;
      } else {
        result += ',';
      }
      result += hp.ToString();
    }
    result += '}';
  }
  return result;
}

CloudInfoPB ServerBaseOptions::MakeCloudInfoPB() const {
  CloudInfoPB result;
  result.set_placement_cloud(placement_cloud());
  result.set_placement_region(placement_region());
  result.set_placement_zone(placement_zone());
  return result;
}

const std::string& ServerBaseOptions::placement_cloud() const {
  return placement_cloud_.empty() ? FLAGS_placement_cloud : placement_cloud_;
}

const std::string& ServerBaseOptions::placement_region() const {
  return placement_region_.empty() ? FLAGS_placement_region : placement_region_;
}

const std::string& ServerBaseOptions::placement_zone() const {
  return placement_zone_.empty() ? FLAGS_placement_zone : placement_zone_;
}

bool ServerBaseOptions::has_placement_cloud() const {
  return !placement_cloud_.empty();
}

void ServerBaseOptions::SetPlacement(std::string cloud, std::string region, std::string zone) {
  placement_cloud_ = std::move(cloud);
  placement_region_ = std::move(region);
  placement_zone_ = std::move(zone);
}

} // namespace server
} // namespace yb
