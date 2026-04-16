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

#pragma once

#include <future>

#include "yb/master/master_ddl.fwd.h"

#include "yb/server/server_fwd.h"
#include "yb/tserver/tserver_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

namespace yb::tserver {

using YsqlLeaderClientListener = std::function<
    Status(const master::RefreshYsqlLeaseInfoPB& lease_refresh_info)>;

class YsqlLeaseClient {
 public:
  YsqlLeaseClient(TabletServer& server, const YsqlLeaderClientListener& listener);
  YsqlLeaseClient(const YsqlLeaseClient& other) = delete;
  void operator=(const YsqlLeaseClient& other) = delete;

  Status Start();
  void Shutdown();
  std::future<Status> RelinquishLease(MonoDelta timeout) const;
  void UpdateMasterAddresses(server::MasterAddressesPtr master_addresses);

  ~YsqlLeaseClient();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::tserver
