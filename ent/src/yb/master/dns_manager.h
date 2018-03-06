// Copyright (c) YugaByte, Inc.
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

#ifndef ENT_SRC_YB_MASTER_DNS_MANAGER_H
#define ENT_SRC_YB_MASTER_DNS_MANAGER_H

#include <aws/core/Aws.h>
#include <aws/route53/Route53Client.h>
#include <unordered_set>
#include <boost/optional.hpp>

#include "yb/rpc/io_thread_pool.h"
#include "yb/rpc/scheduler.h"
#include "yb/util/monotime.h"

namespace yb {
namespace master {

class TSDescriptor;

namespace enterprise {

class DnsManager {
 public:
  static const MonoDelta kDnsSyncPeriod;

  DnsManager();
  DnsManager(const DnsManager&) = delete;
  void operator=(const DnsManager&) = delete;
  ~DnsManager();

  void MayBeUpdateTServersDns(
      const std::vector<std::shared_ptr<yb::master::TSDescriptor>>& live_tservers);

 private:
  bool AreLiveTServersChanged(
      const std::vector<std::shared_ptr<yb::master::TSDescriptor>>& live_tservers);
  void UpdateTServersDns();

  yb::rpc::IoThreadPool pool_;
  Aws::SDKOptions aws_options_;
  boost::optional<Aws::Route53::Route53Client> route53_client_;

  std::unordered_set<std::string> live_tserver_hosts_;
  std::vector<std::string> live_tserver_hosts_new_;
  std::atomic<bool> tservers_dns_update_started_{false};
};

} // namespace enterprise
} // namespace master
} // namespace yb

#endif  // ENT_SRC_YB_MASTER_DNS_MANAGER_H
