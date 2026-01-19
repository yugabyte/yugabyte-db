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

#include "yb/server/server_base_options.h"

#include "yb/tserver/tserver_fwd.h"
#include "yb/tserver/ysql_lease_manager.h"

#include "yb/util/status_fwd.h"

namespace yb::tserver {

class YsqlLeaseClient {
 public:
  YsqlLeaseClient(
      TabletServer& server, YSQLLeaseManager& lease_manager,
      server::MasterAddressesPtr master_addresses);
  YsqlLeaseClient(const YsqlLeaseClient& other) = delete;
  void operator=(const YsqlLeaseClient& other) = delete;

  Status Start();
  Status Stop();
  std::future<Status> RelinquishLease(MonoDelta timeout) const;
  void set_master_addresses(server::MasterAddressesPtr master_addresses);

  ~YsqlLeaseClient();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yb::tserver
