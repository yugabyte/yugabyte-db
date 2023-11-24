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

#include "yb/consensus/raft_consensus.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_int32(client_read_write_timeout_ms);
DECLARE_int32(retryable_request_timeout_secs);
DECLARE_int32(ysql_client_read_write_timeout_ms);

namespace yb {
namespace pgwrapper {

class PgRetryableRequestTest : public pgwrapper::PgMiniTestBase {
 protected:
  void SetUp() override {
    PgMiniTestBase::SetUp();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 10;
  }

  Result<tablet::TabletPeerPtr> GetOnlyTablePeer() {
    auto peers = ListTabletPeers(cluster_.get(), ListPeersFilter::kLeaders);
    for (const auto& peer : peers) {
      if (!peer->tablet()->regular_db()) {
        continue;
      }
      return peer;
    }
    return STATUS(NotFound, "no tablet peer of user table is found");
  }
};

TEST_F(PgRetryableRequestTest, YsqlRequestTimeoutSecs) {
  auto conn = ASSERT_RESULT(Connect());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 360;

  // When FLAGS_ysql_client_read_write_timeout_ms is specified,
  // YSQL table's retryable request timeout is:
  // Min(FLAGS_retryable_request_timeout_secs, FLAGS_ysql_client_read_write_timeout_ms).
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_client_read_write_timeout_ms) = 5000;
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  auto peer = ASSERT_RESULT(GetOnlyTablePeer());
  ASSERT_EQ(peer->shared_raft_consensus()->TEST_RetryableRequestTimeoutSecs(), 5);
  ASSERT_OK(conn.Execute("DROP TABLE t"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_client_read_write_timeout_ms) = 15000;
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  peer = ASSERT_RESULT(GetOnlyTablePeer());
  ASSERT_EQ(peer->shared_raft_consensus()->TEST_RetryableRequestTimeoutSecs(), 15);
  ASSERT_OK(conn.Execute("DROP TABLE t"));

  // When FLAGS_ysql_client_read_write_timeout_ms is not specified,
  // YSQL table's default retryable request timeout is Max(client_read_write_timeout_ms, 600s).
  // And the final timeout is Min(FLAGS_retryable_request_timeout_secs,
  // default retryable request timeout + 60s). See RetryableRequestTimeoutSecs().
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_client_read_write_timeout_ms) = -1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 5000;
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  peer = ASSERT_RESULT(GetOnlyTablePeer());
  ASSERT_EQ(peer->shared_raft_consensus()->TEST_RetryableRequestTimeoutSecs(), 360);
  ASSERT_OK(conn.Execute("DROP TABLE t"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 700000;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 660;
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  peer = ASSERT_RESULT(GetOnlyTablePeer());
  ASSERT_EQ(peer->shared_raft_consensus()->TEST_RetryableRequestTimeoutSecs(), 660);
  ASSERT_OK(conn.Execute("DROP TABLE t"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 620000;
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  peer = ASSERT_RESULT(GetOnlyTablePeer());
  ASSERT_EQ(peer->shared_raft_consensus()->TEST_RetryableRequestTimeoutSecs(), 620);
  ASSERT_OK(conn.Execute("DROP TABLE t"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_client_read_write_timeout_ms) = 590000;
  ASSERT_OK(conn.Execute(
      "CREATE TABLE t (key INT PRIMARY KEY, v1 TEXT, v2 TEXT) SPLIT INTO 1 TABLETS"));
  peer = ASSERT_RESULT(GetOnlyTablePeer());
  ASSERT_EQ(peer->shared_raft_consensus()->TEST_RetryableRequestTimeoutSecs(), 600);
  ASSERT_OK(conn.Execute("DROP TABLE t"));
}

} // namespace pgwrapper
} // namespace yb
