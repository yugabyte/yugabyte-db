// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.

#include <string_view>

#include "yb/common/entity_ids.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/wire_protocol.h"

#include "yb/master/master_cluster.proxy.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/util/scope_exit.h"

#include "yb/yql/pggate/test/pggate_test.h"
#include "yb/yql/pggate/util/ybc_guc.h"
#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb::pggate {

class PggateAuthSnapshotTest : public PggateTest {
 protected:
  void CustomizeExternalMiniCluster(ExternalMiniClusterOptions* options) override {
    options->enable_ysql = true;
    options->extra_master_flags.push_back("--ysql_enable_auth_catalog_follower_reads=false");
    options->extra_master_flags.push_back("--ysql_enable_catalog_follower_read_reservation=true");
    options->extra_tserver_flags.push_back("--ysql_enable_auth_catalog_follower_reads=false");
    options->extra_tserver_flags.push_back(
        "--ysql_enable_read_request_cache_for_connection_auth=false");
  }

  Status InitReservedCluster() {
    RETURN_NOT_OK(Init("PggateAuthSnapshotTest", 1, 1, false /* should_create_db */));
    auto proxy = cluster_->GetLeaderMasterProxy<master::MasterClusterProxy>();
    master::ReserveYsqlCatalogFollowerReadsRequestPB req;
    req.set_acknowledge_permanent_pitr_exclusion(true);
    master::ReserveYsqlCatalogFollowerReadsResponsePB resp;
    rpc::RpcController rpc;
    rpc.set_timeout(MonoDelta::FromSeconds(30 * kTimeMultiplier));
    RETURN_NOT_OK(proxy.ReserveYsqlCatalogFollowerReads(req, &resp, &rpc));
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    RETURN_NOT_OK(cluster_->SetFlag(
        cluster_->GetLeaderMaster(), "ysql_enable_auth_catalog_follower_reads", "true"));
    return cluster_->SetFlag(
        cluster_->tablet_server(0), "ysql_enable_auth_catalog_follower_reads", "true");
  }

  static YbcReadHybridTime ReadTime(uint64_t time) {
    return {time, time, time, HybridTime::kMax.ToUint64(), 0};
  }

  Status PrefetchAuthId() {
    YBCRegisterSysTableForPrefetching(
        kTemplate1Oid, 1260 /* pg_authid */, kInvalidOid, 0, false);
    return Status(YBCPrefetchRegisteredSysTables(), AddRef::kFalse);
  }

  Result<bool> ReadRoleLogin() {
    YbcPgStatement statement;
    RETURN_NOT_OK(Status(YBCPgNewSelect(
        kTemplate1Oid, 1260 /* pg_authid */, nullptr, kDefaultTableLocality, {}, &statement),
        AddRef::kFalse));
    YbcPgExpr name, login;
    RETURN_NOT_OK(Status(YBCTestNewColumnRef(
        statement, 2 /* rolname */, DataType::STRING, &name), AddRef::kFalse));
    RETURN_NOT_OK(Status(YBCPgDmlAppendTarget(statement, name, false), AddRef::kFalse));
    RETURN_NOT_OK(Status(YBCTestNewColumnRef(
        statement, 7 /* rolcanlogin */, DataType::BOOL, &login), AddRef::kFalse));
    RETURN_NOT_OK(Status(YBCPgDmlAppendTarget(statement, login, false), AddRef::kFalse));
    RETURN_NOT_OK(Status(YBCPgExecSelect(statement, nullptr), AddRef::kFalse));
    for (;;) {
      uint64_t values[7] = {};
      bool is_null[7] = {};
      bool has_data = false;
      RETURN_NOT_OK(Status(
          YBCPgDmlFetch(statement, 7, values, is_null, nullptr, &has_data), AddRef::kFalse));
      SCHECK(has_data, NotFound, "Role login state is missing");
      SCHECK(!is_null[1] && !is_null[6], IllegalState, "Role login state is null");
      if (std::string_view(reinterpret_cast<const char*>(values[1])) == "auth_snapshot_role") {
        return values[6] != 0;
      }
    }
  }
};

TEST_F(PggateAuthSnapshotTest, HistoricalContextCannotReplaceAuthenticationSnapshot) {
  ASSERT_OK(InitReservedCluster());
  const auto historical_time = ReadTime(YBCGetCurrentHybridTimeLsn());
  YBCPgSetHistoricalReadContext(historical_time, nullptr);
  auto cleanup = ScopeExit([] {
    YBCPgResetHistoricalReadContext();
    YBCEndAuthCatalogRead();
  });
  ASSERT_NOK_STR_CONTAINS(
      Status(YBCStartAuthSysTablePrefetching(), AddRef::kFalse), "historical read context");
  ASSERT_FALSE(YBCIsAuthCatalogRead());
  ASSERT_FALSE(YBCIsSysTablePrefetchingStarted());

  YBCPgResetHistoricalReadContext();
  ASSERT_OK(Status(YBCStartAuthSysTablePrefetching(), AddRef::kFalse));
  const auto snapshot = YBCGetPgCatalogReadTime();
  YBCPgSetHistoricalReadContext(historical_time, nullptr);
  ASSERT_NOK_STR_CONTAINS(PrefetchAuthId(), "uncached fixed catalog snapshot");
  ASSERT_TRUE(YBCIsAuthCatalogRead());
  ASSERT_EQ(YBCGetPgCatalogReadTime().read, snapshot.read);
}

TEST_F(PggateAuthSnapshotTest, HistoricalAndExplicitReadsWorkAfterAuthentication) {
  ASSERT_OK(InitReservedCluster());
  auto admin = ASSERT_RESULT(PgConnect("yugabyte"));
  ASSERT_OK(admin.Execute("CREATE ROLE auth_snapshot_role LOGIN"));
  const auto historical_time = ReadTime(YBCGetCurrentHybridTimeLsn());
  ASSERT_OK(admin.Execute("ALTER ROLE auth_snapshot_role NOLOGIN"));

  const auto saved_read_time = yb_read_time;
  const auto saved_is_ht = yb_is_read_time_ht;
  auto cleanup = ScopeExit([&] {
    YBCPgResetHistoricalReadContext();
    YBCEndAuthCatalogRead();
    yb_read_time = saved_read_time;
    yb_is_read_time_ht = saved_is_ht;
  });
  yb_read_time = historical_time.read;
  yb_is_read_time_ht = true;
  ASSERT_OK(Status(YBCStartAuthSysTablePrefetching(), AddRef::kFalse));
  const auto snapshot = YBCGetPgCatalogReadTime();
  ASSERT_GT(snapshot.read, historical_time.read);
  ASSERT_FALSE(ASSERT_RESULT(ReadRoleLogin()));
  ASSERT_EQ(YBCGetPgCatalogReadTime().read, snapshot.read);
  YBCEndAuthCatalogRead();
  ASSERT_FALSE(YBCIsAuthCatalogRead());

  // An explicit session time takes effect only after the authentication scope ends.
  ASSERT_TRUE(ASSERT_RESULT(ReadRoleLogin()));
  yb_read_time = 0;
  YBCPgResetCatalogReadTime();
  ASSERT_FALSE(ASSERT_RESULT(ReadRoleLogin()));

  // Historical reads still take precedence over the session time outside authentication.
  yb_read_time = snapshot.read;
  YBCPgSetHistoricalReadContext(historical_time, nullptr);
  ASSERT_TRUE(ASSERT_RESULT(ReadRoleLogin()));
  YBCPgResetHistoricalReadContext();
  YBCPgResetCatalogReadTime();
  ASSERT_FALSE(ASSERT_RESULT(ReadRoleLogin()));
}

}  // namespace yb::pggate
