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

#include "yb/integration-tests/upgrade-tests/upgrade_test_base.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

namespace yb {

namespace {

constexpr auto kExtensionVersion = "SELECT extversion FROM pg_extension WHERE extname = 'citext'";
constexpr auto kSorted = "SELECT string_agg(name::text, ' ' ORDER BY name) FROM t";
constexpr auto kNumDistinct = "SELECT count(DISTINCT name) FROM t";
constexpr auto kNumGroups = "SELECT count(*) FROM (SELECT name FROM t GROUP BY name) g";
constexpr auto kNumExtensions = "SELECT count(*) FROM pg_extension WHERE extname = 'citext'";
constexpr auto kNoCitextDatabase = "no_citext";

// Without an lsm opclass for citext, the type cache falls back to lsm text_ops, so sorting and
// grouping compare bytes (#36118).
constexpr auto kByteOrder = "A B a b";
constexpr auto kCitextOrder = "A a b B";

} // namespace

class CitextV116UpgradeTest : public UpgradeTestBase {
 public:
  CitextV116UpgradeTest() : UpgradeTestBase(kBuild_2025_1_1_0) {}

 protected:
  // On the old version: citext 1.6, a table with two pairs of values that differ only in case,
  // and a second database without citext.
  Status CreateCitextTable() {
    auto conn = VERIFY_RESULT(cluster_->ConnectToDB());
    RETURN_NOT_OK(conn.Execute("CREATE EXTENSION citext"));
    SCHECK_EQ(VERIFY_RESULT(conn.FetchRow<std::string>(kExtensionVersion)), "1.6", IllegalState,
              "Unexpected citext version");
    RETURN_NOT_OK(conn.Execute("CREATE TABLE t (id int PRIMARY KEY, name citext)"));
    RETURN_NOT_OK(conn.Execute("INSERT INTO t VALUES (1, 'b'), (2, 'A'), (3, 'a'), (4, 'B')"));
    return conn.ExecuteFormat("CREATE DATABASE $0", kNoCitextDatabase);
  }

  // Checks that sorting on t follows `order` and that DISTINCT and GROUP BY find `groups` values.
  void CheckSemantics(pgwrapper::PGConn& conn, const std::string& order, int64_t groups) {
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(kSorted)), order);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(kNumDistinct)), groups);
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<int64_t>(kNumGroups)), groups);
  }
};

TEST_F(CitextV116UpgradeTest, UpdateExistingInstall) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(CreateCitextTable());
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_NO_FATALS(CheckSemantics(conn, kByteOrder, 4));
  }

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));

  // Until the upgrade is finalized, the catalog is unchanged, so every node gives the old results.
  // open_conn stays open across finalize, to check that a session does not have to reconnect to
  // see the new opclass.
  auto open_conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_EQ(ASSERT_RESULT(open_conn.FetchRow<std::string>(kExtensionVersion)), "1.6");
  ASSERT_NO_FATALS(CheckSemantics(open_conn, kByteOrder, 4));

  ASSERT_OK(FinalizeUpgrade());
  {
    auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
    ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(kExtensionVersion)), "1.6-yb-1.0");
    ASSERT_NO_FATALS(CheckSemantics(conn, kCitextOrder, 2));

    // The migration leaves databases without citext alone.
    auto no_citext_conn = ASSERT_RESULT(cluster_->ConnectToDB(kNoCitextDatabase));
    ASSERT_EQ(ASSERT_RESULT(no_citext_conn.FetchRow<int64_t>(kNumExtensions)), 0);
  }
  ASSERT_NO_FATALS(CheckSemantics(open_conn, kCitextOrder, 2));
}

// Rolling back before finalize leaves citext at 1.6, since the migration only runs at finalize.
TEST_F(CitextV116UpgradeTest, RollbackBeforeFinalize) {
  ASSERT_OK(StartClusterInOldVersion());
  ASSERT_OK(CreateCitextTable());

  ASSERT_OK(UpgradeClusterToCurrentVersion(kNoDelayBetweenNodes, /*auto_finalize=*/false));
  ASSERT_OK(RollbackClusterToOldVersion(kNoDelayBetweenNodes));

  auto conn = ASSERT_RESULT(cluster_->ConnectToDB());
  ASSERT_EQ(ASSERT_RESULT(conn.FetchRow<std::string>(kExtensionVersion)), "1.6");
  ASSERT_NO_FATALS(CheckSemantics(conn, kByteOrder, 4));
}

} // namespace yb
