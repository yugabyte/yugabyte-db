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
// Tests for the YbInternalConnKind registry framework: verify that the dedicated
// YB_RELCACHE_INIT_BACKEND BackendType is assigned to connections that opt in via the
// yb_internal_conn_kind startup parameter, and that other connections (regular client or
// other internal kinds) are not mistakenly identified as the relcache-init builder. This
// pins the registry-driven discriminator that both the minimal-preload gate
// (pg_yb_utils.c YbUseMinimalCatalogCachesPreload) and the recursion gate
// (relcache.c RelationCacheInitializePhase3) key off.

#include <optional>
#include <string>

#include <gtest/gtest.h>

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/test_macros.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

namespace yb::pgwrapper {

class PgRelcacheInitInternalConnTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override { return 1; }

  // Open a tserver-owned internal connection through the local unix socket as user=postgres
  // with the shared-memory postgres auth key, so the hardcoded
  //
  //     local all postgres yb-tserver-key
  //
  // HBA rule matches and the connection authenticates via real yb-tserver-key auth (the
  // same path tserver-side code takes in production). No TEST bypass flag is involved.
  //
  // Pass an empty yb_internal_conn_kind_wire_name to skip the kind parameter -- the
  // connection still uses yb-tserver-key auth (via the hardcoded HBA rule) but the
  // backend type stays YB_BACKEND-equivalent (BackendType is decided from the kind).
  Result<PGConn> ConnectAs(std::string_view yb_internal_conn_kind_wire_name) {
    auto* ts = cluster_->mini_tablet_server(0)->server();
    return CreateInternalPGConnBuilder(
        ts->pgsql_proxy_bind_address(), /*database_name=*/"yugabyte", /*user=*/"postgres",
        ts->GetSharedMemoryPostgresAuthKey(), /*deadline=*/std::nullopt,
        /*yb_auto_analyze=*/false, yb_internal_conn_kind_wire_name).Connect();
  }

  static Result<std::string> CurrentBackendType(PGConn* conn) {
    return conn->FetchRow<std::string>(
        "SELECT backend_type FROM pg_stat_activity WHERE pid = pg_backend_pid()");
  }
};

// A connection that carries yb_internal_conn_kind="relcache_init" must materialize as the
// dedicated YB_RELCACHE_INIT_BACKEND. Both the minimal-preload gate and the recursion gate
// read MyBackendType, so verifying backend_type here proves the discriminator is wired up
// from libpq through the postmaster and the registry.
TEST_F(PgRelcacheInitInternalConnTest, RelcacheInitConnGetsDedicatedBackendType) {
  auto conn = ASSERT_RESULT(ConnectAs(YbInternalConnKindWireName::kRelcacheInit));
  const auto backend_type = ASSERT_RESULT(CurrentBackendType(&conn));
  ASSERT_EQ(backend_type, "yb relcache init backend");
}

// A plain client connection must not be confused with the relcache-init backend.
TEST_F(PgRelcacheInitInternalConnTest, RegularConnIsNotRelcacheInitBackend) {
  auto conn = ASSERT_RESULT(Connect());
  const auto backend_type = ASSERT_RESULT(CurrentBackendType(&conn));
  ASSERT_NE(backend_type, "yb relcache init backend");
  ASSERT_EQ(backend_type, "client backend");
}

// An unrecognized yb_internal_conn_kind value must be rejected at startup; we should not
// silently coerce it to "regular client" because that masks misconfiguration.
TEST_F(PgRelcacheInitInternalConnTest, UnknownInternalConnKindIsRejected) {
  auto result = ConnectAs(/*yb_internal_conn_kind_wire_name=*/"not_a_real_kind");
  ASSERT_NOK(result);
  ASSERT_STR_CONTAINS(result.status().ToString(), "yb_internal_conn_kind");
}

}  // namespace yb::pgwrapper
