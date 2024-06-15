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
//

#pragma once

#include "yb/integration-tests/cql_test_util.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/tools/tools_test_utils.h"
#include "yb/yql/cql/cqlserver/cql_server.h"

namespace yb {

template <class MiniClusterType>
class CqlTestBase : public MiniClusterTestWithClient<MiniClusterType> {
 public:
  static constexpr auto kDefaultNumMasters = 1;
  static constexpr auto kDefaultNumTabletServers = 3;

  virtual ~CqlTestBase() = default;

  virtual int num_masters() {
    return kDefaultNumMasters;
  }

  virtual int num_tablet_servers() {
    return kDefaultNumTabletServers;
  }

  void SetUp() override;

  Status RestartCluster();
  void ShutdownCluster();
  Status StartCluster();

  std::string GetTempDir(const std::string& subdir) {
    return tmp_dir_ / subdir;
  }

  Status RunBackupCommand(const std::vector<std::string>& args);

  static std::unique_ptr<cqlserver::CQLServer> MakeCQLServerForTServer(
      MiniClusterType* cluster, int ts_idx, client::YBClient* client, std::string* cql_host,
      uint16_t* cql_port);

 protected:
  void DoTearDown() override;

  virtual void SetUpFlags() {}

  std::unique_ptr<CppCassandraDriver> driver_;
  std::unique_ptr<cqlserver::CQLServer> cql_server_;
  typename MiniClusterType::Options mini_cluster_opt_;

 private:
  void SetupClusterOpt();
  Status StartCQLServer();

  std::string cql_host_;
  uint16_t cql_port_ = 0;
  tools::TmpDirProvider tmp_dir_;
};

} // namespace yb
