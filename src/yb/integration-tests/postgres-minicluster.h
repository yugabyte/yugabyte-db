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

#include "yb/client/transaction_manager.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_wrapper.h"

namespace yb {
// Utility class to use postgres with MiniCluster in tests
class PostgresMiniCluster {
 public:
  std::unique_ptr<MiniCluster> mini_cluster_;
  std::unique_ptr<client::YBClient> client_;
  std::unique_ptr<yb::pgwrapper::PgSupervisor> pg_supervisor_;
  HostPort pg_host_port_;
  boost::optional<client::TransactionManager> txn_mgr_;
  Result<pgwrapper::PGConn> Connect();
  Result<pgwrapper::PGConn> ConnectToDB(
      const std::string& dbname, bool simple_query_protocol = false);
  Result<pgwrapper::PGConn> ConnectToDBWithReplication(const std::string& dbname);
};
}  // namespace yb
