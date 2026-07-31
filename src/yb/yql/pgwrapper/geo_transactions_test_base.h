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

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/common/common_net.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/pg_types.h"
#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace pgwrapper {
class PGConn;
}  // namespace pgwrapper

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
  static const inline std::string kTableName = kTablePrefix + "_tbl";
  static const inline std::string kIndexName = kTablePrefix + "_idx";
  static const inline std::string kMatViewName = kTablePrefix + "_mv";

  static constexpr auto kLocalRegion = 1;
  static constexpr auto kOtherRegion = 2;

  void SetUp() override;

  void InitTransactionManagerAndPool();

  size_t NumTabletServers() override { return NumRegions(); }

  virtual size_t NumRegions() { return 3; }

 protected:
  uint64_t GetCurrentVersion();

  void CreateTransactionTable(int region);

  Result<TableId> GetTransactionTableId(int region);

  Result<TableId> GetTransactionTableId(const std::string& name);

  void StartDeleteTransactionTable(std::string_view tablespace);

  void WaitForDeleteTransactionTableToFinish(std::string_view tablespace);

  void CreateMultiRegionTransactionTable();

  virtual void SetupTablespaces();

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

  void ValidateAllTabletLeaderInZone(std::vector<TabletId> tablet_uuids, int region);
  bool AllTabletLeaderInZone(std::vector<TabletId> tablet_uuids, int region);

  static Status WarmupTablespaceCache(pgwrapper::PGConn& conn, std::string_view table);

  Result<PgTablespaceOid> GetTablespaceOid(std::string_view tablespace) const;
  Result<PgTablespaceOid> GetTablespaceOidForRegion(int region) const;
  Result<std::vector<TabletId>> GetStatusTabletsWithTableName(
      const std::string& local_txn_table, ExpectedLocality expected);
  Result<std::vector<TabletId>> GetStatusTablets(
      std::string_view tablespace, ExpectedLocality locality);
  Result<std::vector<TabletId>> GetStatusTablets(int region, ExpectedLocality locality);

  TransactionManager* transaction_manager_;
  TransactionPool* transaction_pool_;
  size_t tables_per_region_ = 0;
  std::vector<CloudInfoPB> tserver_placements_;
};

} // namespace client
} // namespace yb
