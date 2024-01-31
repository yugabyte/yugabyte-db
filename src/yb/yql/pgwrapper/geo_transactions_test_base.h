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

#pragma once

#include <optional>

#include "yb/client/client_fwd.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb {

namespace client {

YB_DEFINE_ENUM(ExpectedLocality, (kLocal)(kGlobal)(kNoCheck));
YB_STRONGLY_TYPED_BOOL(SetGlobalTransactionsGFlag);
YB_STRONGLY_TYPED_BOOL(SetGlobalTransactionSessionVar);
YB_STRONGLY_TYPED_BOOL(WaitForHashChange);
YB_STRONGLY_TYPED_BOOL(InsertToLocalFirst);

class TransactionManager;
class TransactionPool;

class GeoTransactionsTestBase : public pgwrapper::PgMiniTestBase {
 public:
  static const inline std::string kTablePrefix = "test";
  static constexpr auto kLocalRegion = 1;
  static constexpr auto kOtherRegion = 2;

  void SetUp() override;

  void InitTransactionManagerAndPool();

  size_t NumTabletServers() override { return NumRegions(); }

  virtual size_t NumRegions() { return 3; }

 protected:
  const std::shared_ptr<tserver::MiniTabletServer> PickPgTabletServer(
      const MiniCluster::MiniTabletServers& servers) override;

  uint64_t GetCurrentVersion();

  void CreateTransactionTable(int region);

  Result<TableId> GetTransactionTableId(int region);

  void StartDeleteTransactionTable(int region);

  void WaitForDeleteTransactionTableToFinish(int region);

  void CreateMultiRegionTransactionTable();

  void SetupTablespaces();

  virtual void SetupTables(size_t tables_per_region);

  void SetupTablesAndTablespaces(size_t tables_per_region);

  void DropTablespaces();

  virtual void DropTables();

  void DropTablesAndTablespaces();

  void WaitForStatusTabletsVersion(uint64_t version);

  void WaitForLoadBalanceCompletion();

  Status StartTabletServersByRegion(int region);
  Status ShutdownTabletServersByRegion(int region);
  Status StartTabletServers(
    const std::optional<std::string>& region_str, const std::optional<std::string>& zone_str);
  Status ShutdownTabletServers(
    const std::optional<std::string>& region_str, const std::optional<std::string>& zone_str);
  Status StartShutdownTabletServers(
    const std::optional<std::string>& region_str, const std::optional<std::string>& zone_str,
    bool shutdown);

  void ValidateAllTabletLeaderinZone(std::vector<TabletId> tablet_uuids, int region);
  Result<uint32_t> GetTablespaceOidForRegion(int region);
  Result<std::vector<TabletId>> GetStatusTablets(int region, ExpectedLocality locality);

  TransactionManager* transaction_manager_;
  TransactionPool* transaction_pool_;
  size_t tables_per_region_ = 0;
  std::vector<CloudInfoPB> tserver_placements_;
};

} // namespace client
} // namespace yb
