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
// Live smoke test for the Perform-based tserver client (src/yb/thin_client/yb_thin_client.h) driven
// against a single mini tserver's PgClientService. Exercises the whole C ABI: client_create ->
// table_open (schema check) -> upsert_batch -> paged read, and cross-checks the shim's writes and
// reads against ordinary SQL through the same tserver.

#include <array>
#include <future>
#include <string>
#include <vector>

#include "yb/thin_client/yb_thin_client.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/format.h"
#include "yb/util/net/net_util.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(use_node_to_node_encryption);
DECLARE_bool(use_client_to_server_encryption);
DECLARE_bool(allow_insecure_connections);
DECLARE_bool(TEST_private_broadcast_address);
DECLARE_string(certs_dir);
DECLARE_string(TEST_public_hostname_suffix);

namespace yb::pgwrapper {

namespace {

// A decoded cell, deep-copied so it outlives ybthin_read_result_free.
struct DecodedCell {
  ybthin_bind_tag tag = YBTHIN_BIND_NULL;
  int64_t i = 0;
  std::string bytes;
};

// Bridges an async shim read to a future. `cells` is row-major (n_rows * n_cols).
struct ReadOutcome {
  ybthin_status_code code = YBTHIN_OTHER;
  std::string message;
  size_t n_rows = 0;
  size_t n_cols = 0;
  std::vector<DecodedCell> cells;
  std::vector<uint8_t> paging_state;
  uint64_t used_read_time_ht = 0;
};

void OnReadDone(void* ctx, ybthin_status status, ybthin_read_result* result) {
  auto* promise = static_cast<std::promise<ReadOutcome>*>(ctx);
  ReadOutcome out;
  out.code = status.code;
  if (status.message) {
    out.message = status.message;
    ybthin_string_free(status.message);
  }
  if (result) {
    out.used_read_time_ht = result->used_read_time_ht;
    // This test drives a single-op batch, so read results[0].
    if (result->n_ops > 0) {
      const ybthin_read_op_result& op = result->results[0];
      out.n_rows = op.n_rows;
      out.n_cols = op.n_cols;
      out.cells.resize(op.n_rows * op.n_cols);
      for (size_t idx = 0; idx < out.cells.size(); ++idx) {
        const ybthin_cell& src = op.cells[idx];
        DecodedCell& dst = out.cells[idx];
        dst.tag = src.tag;
        dst.i = src.i;
        if (src.bytes && src.bytes_len) {
          dst.bytes.assign(reinterpret_cast<const char*>(src.bytes), src.bytes_len);
        }
      }
      if (op.paging_state) {
        out.paging_state.assign(op.paging_state, op.paging_state + op.paging_state_len);
      }
    }
    ybthin_read_result_free(result);
  }
  promise->set_value(std::move(out));
}

struct WriteOutcome {
  ybthin_status_code code = YBTHIN_OTHER;
  std::string message;
};

void OnWriteDone(void* ctx, ybthin_status status) {
  auto* promise = static_cast<std::promise<WriteOutcome>*>(ctx);
  WriteOutcome out;
  out.code = status.code;
  if (status.message) {
    out.message = status.message;
    ybthin_string_free(status.message);
  }
  promise->set_value(std::move(out));
}

ybthin_bind I32(int32_t v) { return ybthin_bind{YBTHIN_BIND_I32, v, nullptr, 0}; }

ybthin_bind Bytea(const std::string& s) {
  return ybthin_bind{
      YBTHIN_BIND_BYTEA, 0, reinterpret_cast<const uint8_t*>(s.data()), s.size()};
}

}  // namespace

class PgThinClientTest : public PgMiniTestBase {
 protected:
  // These tests exercise single-shard ops against one hash key; a single tserver keeps the
  // in-process cluster small enough that running the plaintext and TLS cases back-to-back in one
  // test binary does not exhaust memory when initdb forks postgres.
  size_t NumTabletServers() override { return 1; }

  std::string TServerAddr() const {
    return cluster_->mini_tablet_server(0)->bound_rpc_addr_str();
  }

  Result<uint32_t> FetchOid(PGConn* conn, const std::string& query) {
    auto value = VERIFY_RESULT(conn->FetchRowAsString(query));
    return static_cast<uint32_t>(std::stoul(value));
  }

  // Drives the full C ABI end-to-end (open table, upsert, paged read) against the mini tserver,
  // connecting with `tls` (nullptr => plaintext) and cross-checking writes and reads via SQL.
  void RunOpenUpsertReadPaged(const ybthin_tls_opts* tls) {
    constexpr int kHashKey = 1;
    constexpr int kOtherHashKey = 2;
    constexpr int kNumRows = 250;
    constexpr int kUpperBound = 199;      // read v <= 199
    constexpr int kExpectedRead = 200;    // v in [0, 199]
    constexpr uint64_t kPageLimit = 64;

    // Create the table and populate a decoy hash key via SQL so we can cross-check the shim.
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute(
        "CREATE TABLE t (k int, v int, payload bytea, PRIMARY KEY((k) HASH, v))"));
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO t SELECT $0, gs, '\\xdead'::bytea FROM generate_series(0, 9) gs",
        kOtherHashKey));

    const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                       "WHERE datname = current_database()"));
    const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 't'::regclass::oid"));

    // ---- client_create ------------------------------------------------------
    const auto addr = TServerAddr();
    const char* addrs[] = {addr.c_str()};
    ybthin_client* client = nullptr;
    {
      auto st = ybthin_client_create(
          addrs, 1, tls, /* pool= */ nullptr, /* rpc_timeout_ms= */ 60000, /* num_reactors= */ 0,
          &client);
      ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
    }

    // ---- table_open + schema check -----------------------------------------
    ybthin_table* table = nullptr;
    ybthin_table_info info = {};
    {
      auto st = ybthin_table_open(client, db_oid, table_oid, &table, &info);
      ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
    }
    ASSERT_EQ(info.n_columns, 3);
    ASSERT_EQ(std::string(info.columns[0].name), "k");
    ASSERT_EQ(info.columns[0].kind, YBTHIN_COL_HASH);
    ASSERT_EQ(info.columns[0].type, YBTHIN_T_I32);
    ASSERT_EQ(std::string(info.columns[1].name), "v");
    ASSERT_EQ(info.columns[1].kind, YBTHIN_COL_RANGE);
    ASSERT_EQ(info.columns[1].type, YBTHIN_T_I32);
    ASSERT_EQ(std::string(info.columns[2].name), "payload");
    ASSERT_EQ(info.columns[2].kind, YBTHIN_COL_VALUE);
    ASSERT_EQ(info.columns[2].type, YBTHIN_T_BYTEA);

    const int32_t v_id = info.columns[1].id;
    const int32_t payload_id = info.columns[2].id;

    // ---- upsert_batch -------------------------------------------------------
    const std::string payload = "hello";
    std::vector<std::array<ybthin_bind, 2>> keys(kNumRows);
    std::vector<ybthin_bind> values(kNumRows);
    std::vector<int32_t> value_ids(kNumRows, payload_id);
    std::vector<ybthin_upsert_row> rows(kNumRows);
    for (int i = 0; i < kNumRows; ++i) {
      keys[i] = {I32(kHashKey), I32(i)};      // (k HASH, v RANGE) in schema order
      values[i] = Bytea(payload);
      rows[i] = ybthin_upsert_row{table, keys[i].data(), 2, &value_ids[i], &values[i], 1};
    }
    {
      std::promise<WriteOutcome> promise;
      auto future = promise.get_future();
      ybthin_upsert_batch_async(client, rows.data(), rows.size(), &OnWriteDone, &promise);
      auto out = future.get();
      ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
    }

    // Cross-check the shim's writes via SQL through the same tserver.
    ASSERT_EQ(kNumRows, ASSERT_RESULT(conn.FetchRow<PGUint64>(
                            Format("SELECT count(*) FROM t WHERE k = $0", kHashKey))));
    ASSERT_EQ(
        kExpectedRead,
        ASSERT_RESULT(conn.FetchRow<PGUint64>(Format(
            "SELECT count(*) FROM t WHERE k = $0 AND v <= $1", kHashKey, kUpperBound))));

    // ---- paged read ---------------------------------------------------------
    ybthin_bind hash_values[] = {I32(kHashKey)};
    ybthin_cond conds[] = {{v_id, YBTHIN_LE, I32(kUpperBound)}};
    int32_t target_ids[] = {v_id, payload_id};
    ybthin_read_spec spec = {};
    spec.hash_values = hash_values;
    spec.n_hash = 1;
    spec.conds = conds;
    spec.n_conds = 1;
    spec.target_ids = target_ids;
    spec.n_targets = 2;
    spec.limit = kPageLimit;
    spec.is_forward_scan = 1;

    int64_t total = 0;
    int pages = 0;
    std::vector<uint8_t> paging_state;
    do {
      std::promise<ReadOutcome> promise;
      auto future = promise.get_future();
      // Single-op batch, paging this op's scan across calls (the batch snapshot is per-call here).
      ybthin_read_op op = {};
      op.table = table;
      op.spec = spec;
      op.paging_state_in = paging_state.empty() ? nullptr : paging_state.data();
      op.paging_state_in_len = paging_state.size();
      ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
      auto out = future.get();
      ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
      // The shim decoded the sidecar into typed cells: 2 targets (v INT4, payload BYTEA).
      ASSERT_EQ(out.n_cols, 2u);
      ASSERT_EQ(out.cells.size(), out.n_rows * out.n_cols);
      for (size_t r = 0; r < out.n_rows; ++r) {
        const DecodedCell& v_cell = out.cells[r * out.n_cols + 0];
        const DecodedCell& payload_cell = out.cells[r * out.n_cols + 1];
        ASSERT_EQ(v_cell.tag, YBTHIN_BIND_I32);
        EXPECT_LE(v_cell.i, kUpperBound);  // the range bound we scanned under
        ASSERT_EQ(payload_cell.tag, YBTHIN_BIND_BYTEA);
        EXPECT_EQ(payload_cell.bytes, payload);
      }
      total += static_cast<int64_t>(out.n_rows);
      paging_state = std::move(out.paging_state);
      ++pages;
      ASSERT_LT(pages, 100) << "paging did not terminate";
    } while (!paging_state.empty());

    ASSERT_EQ(total, kExpectedRead);
    ASSERT_GT(pages, 1) << "expected the scan to span multiple pages at limit " << kPageLimit;

    ybthin_columns_free(info.columns, info.n_columns);
    ybthin_table_close(table);
    ybthin_client_destroy(client);
  }
};

TEST_F(PgThinClientTest, OpenUpsertReadPaged) {
  RunOpenUpsertReadPaged(/* tls= */ nullptr);
}

// TLS variant: the cluster runs with node-to-node encryption, and the thin client connects over TLS
// authenticating the server against the test CA. Also asserts the TLS-only endpoint rejects a
// plaintext client.
class PgThinClientTlsTest : public PgThinClientTest {
 protected:
  void SetUp() override {
    // Encrypt both the internal RPC path (which the thin client speaks to) and the client-to-server
    // path; enabling only node-to-node leaves the in-process postgres cert setup half-configured.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_client_to_server_encryption) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_insecure_connections) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_certs_dir) = GetCertsDir();
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_public_hostname_suffix) = ".ip.yugabyte";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_private_broadcast_address) = true;
    PgThinClientTest::SetUp();
  }

  std::string CaCertPath() const { return JoinPathSegments(GetCertsDir(), "ca.crt"); }
};

// Exercises the thin client's TLS path against the encrypted tserver RPC endpoint. client_create
// opens a session via a Heartbeat RPC, so a success proves the full TLS handshake plus an encrypted
// RPC round-trip; Perform ops reuse the same TLS stream. The plaintext/Perform coverage lives in
// the non-TLS test above, so this focuses on transport security only.
TEST_F(PgThinClientTlsTest, ClientCreateOverTls) {
  const auto addr = TServerAddr();
  const char* addrs[] = {addr.c_str()};

  // The TLS-only RPC endpoint must reject a plaintext client.
  {
    ybthin_client* insecure = nullptr;
    auto st = ybthin_client_create(
        addrs, 1, /* tls= */ nullptr, /* pool= */ nullptr, /* rpc_timeout_ms= */ 10000,
        /* num_reactors= */ 0, &insecure);
    ASSERT_NE(st.code, YBTHIN_OK) << "plaintext client unexpectedly reached a TLS-only endpoint";
    if (st.message) {
      ybthin_string_free(st.message);
    }
    if (insecure) {
      ybthin_client_destroy(insecure);
    }
  }

  // Server-authenticated TLS using the test CA (no client cert required by default).
  const auto ca = CaCertPath();
  ybthin_tls_opts tls = {};
  tls.ca_cert_path = ca.c_str();
  ybthin_client* client = nullptr;
  auto st = ybthin_client_create(
      addrs, 1, &tls, /* pool= */ nullptr, /* rpc_timeout_ms= */ 60000, /* num_reactors= */ 0,
      &client);
  ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  ybthin_client_destroy(client);
}

}  // namespace yb::pgwrapper
