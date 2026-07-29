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

#include <algorithm>
#include <functional>
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

// Regression test for row drops across page boundaries.
//
// A paged scan must observe ONE consistent snapshot for its whole duration -- every row that
// existed when the scan started must be returned, even if it is deleted while the scan is still
// paging. This test starts a scan, then commits a DELETE of the not-yet-scanned tail between the
// first and second pages; a snapshot-consistent scan must still return those rows (the delete is
// after the scan's snapshot), so nothing is dropped mid-scan.
//
// It continues the scan by only passing the server's paging_state back (read_time_ht == 0 on every
// call -- the natural "just keep paging" usage, and exactly what OpenUpsertReadPaged above does).
// The shim advances its per-session read_time_serial on every Perform, including continuations,
// which makes the server's ENSURE_READ_TIME_IS_SET pick a FRESH (current) read time for each page
// (pg_client_session.cc ProcessReadTimeManipulation) instead of restoring page 1's; that fresh time
// is forwarded to the tablet as an explicit read time, so DocDB ignores the read time embedded in
// the paging_state (pgsql_operation.cc, guarded by !is_explicit_request_read_time) and continues
// the scan at the newer snapshot. Rows deleted between pages therefore vanish from the result --
// the "paged scan drops rows across a page boundary" failure this guards against.
TEST_F(PgThinClientTest, PagedReadHoldsSnapshotAcrossPages) {
  constexpr int kHashKey = 7;
  constexpr int kNumRows = 200;          // v in [0, 199]
  constexpr int kDeleteFrom = 100;       // rows v in [100, 199] deleted after page 1
  constexpr uint64_t kPageLimit = 50;    // forces several pages (first page returns v in [0, 49])

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE p (k int, v int, PRIMARY KEY((k) HASH, v))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO p SELECT $0, gs FROM generate_series(0, $1) gs", kHashKey, kNumRows - 1));

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 'p'::regclass::oid"));

  const auto addr = TServerAddr();
  const char* addrs[] = {addr.c_str()};
  ybthin_client* client = nullptr;
  {
    auto st = ybthin_client_create(
        addrs, 1, /* tls= */ nullptr, /* pool= */ nullptr, /* rpc_timeout_ms= */ 60000,
        /* num_reactors= */ 0, &client);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }
  ybthin_table* table = nullptr;
  ybthin_table_info info = {};
  {
    auto st = ybthin_table_open(client, db_oid, table_oid, &table, &info);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }
  const int32_t v_id = info.columns[1].id;

  ybthin_bind hash_values[] = {I32(kHashKey)};
  int32_t target_ids[] = {v_id};
  ybthin_read_spec spec = {};
  spec.hash_values = hash_values;
  spec.n_hash = 1;
  spec.target_ids = target_ids;
  spec.n_targets = 1;
  spec.limit = kPageLimit;
  spec.is_forward_scan = 1;

  std::vector<int32_t> seen;
  std::vector<uint8_t> paging_state;
  int pages = 0;
  bool deleted = false;
  do {
    std::promise<ReadOutcome> promise;
    auto future = promise.get_future();
    ybthin_read_op op = {};
    op.table = table;
    op.spec = spec;
    op.paging_state_in = paging_state.empty() ? nullptr : paging_state.data();
    op.paging_state_in_len = paging_state.size();
    // Continue the scan with just the paging_state (read_time_ht == 0): the scan must stay on its
    // original snapshot on its own, not silently jump to a newer one on each page.
    ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
    auto out = future.get();
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
    for (size_t r = 0; r < out.n_rows; ++r) {
      const DecodedCell& v_cell = out.cells[r * out.n_cols + 0];
      ASSERT_EQ(v_cell.tag, YBTHIN_BIND_I32);
      seen.push_back(static_cast<int32_t>(v_cell.i));
    }
    paging_state = std::move(out.paging_state);
    ++pages;
    // After the first page, commit a delete of the not-yet-scanned tail. A scan that holds its
    // snapshot must still return these rows on later pages.
    if (!deleted) {
      ASSERT_OK(conn.ExecuteFormat(
          "DELETE FROM p WHERE k = $0 AND v >= $1", kHashKey, kDeleteFrom));
      deleted = true;
    }
    ASSERT_LT(pages, 100) << "paging did not terminate";
  } while (!paging_state.empty());

  std::sort(seen.begin(), seen.end());
  seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
  ASSERT_GT(pages, 1) << "expected the scan to span multiple pages at limit " << kPageLimit;
  ASSERT_EQ(seen.size(), static_cast<size_t>(kNumRows))
      << "paged scan dropped rows across a page boundary: the concurrent delete of v >= "
      << kDeleteFrom << " became visible mid-scan, so the scan did not hold its snapshot";
  EXPECT_EQ(seen.front(), 0);
  EXPECT_EQ(seen.back(), kNumRows - 1);

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
}

// Range-sharded tables. Routing is the client's job on the Perform path (PgClientSession never
// derives a partition key), and a range table has no hash columns to derive one from, so the shim
// builds it from the range key. The table is split into 4 tablets so anything that mis-routes --
// or that fails to follow the paging state onto the next tablet -- loses rows instead of quietly
// passing on a single-tablet table.
TEST_F(PgThinClientTest, RangeShardedTable) {
  constexpr int kNumRows = 200;         // r in [0, 199], split across 4 tablets
  constexpr uint64_t kPageLimit = 32;   // several pages per tablet

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE rs (r int, payload bytea, PRIMARY KEY(r ASC)) "
      "SPLIT AT VALUES ((50), (100), (150))"));

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 'rs'::regclass::oid"));

  const auto addr = TServerAddr();
  const char* addrs[] = {addr.c_str()};
  ybthin_client* client = nullptr;
  {
    auto st = ybthin_client_create(
        addrs, 1, /* tls= */ nullptr, /* pool= */ nullptr, /* rpc_timeout_ms= */ 60000,
        /* num_reactors= */ 0, &client);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }
  ybthin_table* table = nullptr;
  ybthin_table_info info = {};
  {
    auto st = ybthin_table_open(client, db_oid, table_oid, &table, &info);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }
  // No hash columns: the key column is RANGE.
  ASSERT_EQ(info.n_columns, 2);
  ASSERT_EQ(std::string(info.columns[0].name), "r");
  ASSERT_EQ(info.columns[0].kind, YBTHIN_COL_RANGE);
  const int32_t r_id = info.columns[0].id;
  const int32_t payload_id = info.columns[1].id;

  // ---- upserts spanning every tablet --------------------------------------
  const std::string payload = "rangeval";
  std::vector<std::array<ybthin_bind, 1>> keys(kNumRows);
  std::vector<ybthin_bind> values(kNumRows);
  std::vector<int32_t> value_ids(kNumRows, payload_id);
  std::vector<ybthin_upsert_row> rows(kNumRows);
  for (int i = 0; i < kNumRows; ++i) {
    keys[i] = {I32(i)};
    values[i] = Bytea(payload);
    rows[i] = ybthin_upsert_row{table, keys[i].data(), 1, &value_ids[i], &values[i], 1};
  }
  {
    std::promise<WriteOutcome> promise;
    auto future = promise.get_future();
    ybthin_upsert_batch_async(client, rows.data(), rows.size(), &OnWriteDone, &promise);
    auto out = future.get();
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }
  // Cross-check via SQL: every row landed, and on the right tablet (a mis-routed write would be
  // rejected or land under a different key).
  ASSERT_EQ(kNumRows, ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM rs")));
  ASSERT_EQ(kNumRows, ASSERT_RESULT(conn.FetchRow<PGUint64>(
                          Format("SELECT count(*) FROM rs WHERE payload = '$0'::bytea",
                                 payload))));

  // ---- point read: exact range key routes to one tablet --------------------
  {
    ybthin_bind range_values[] = {I32(137)};  // in the 4th tablet
    int32_t target_ids[] = {r_id};
    ybthin_read_spec spec = {};
    spec.range_values = range_values;
    spec.n_range = 1;
    spec.target_ids = target_ids;
    spec.n_targets = 1;
    spec.is_forward_scan = 1;

    std::promise<ReadOutcome> promise;
    auto future = promise.get_future();
    ybthin_read_op op = {};
    op.table = table;
    op.spec = spec;
    ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
    auto out = future.get();
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
    ASSERT_EQ(out.n_rows, 1u);
    ASSERT_EQ(out.cells[0].tag, YBTHIN_BIND_I32);
    EXPECT_EQ(out.cells[0].i, 137);
  }

  // ---- full paged scan must cross every tablet boundary --------------------
  {
    int32_t target_ids[] = {r_id};
    ybthin_read_spec spec = {};
    spec.target_ids = target_ids;   // no range_values: scan from the start of the table
    spec.n_targets = 1;
    spec.limit = kPageLimit;
    spec.is_forward_scan = 1;

    std::vector<int32_t> seen;
    std::vector<uint8_t> paging_state;
    int pages = 0;
    do {
      std::promise<ReadOutcome> promise;
      auto future = promise.get_future();
      ybthin_read_op op = {};
      op.table = table;
      op.spec = spec;
      op.paging_state_in = paging_state.empty() ? nullptr : paging_state.data();
      op.paging_state_in_len = paging_state.size();
      ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
      auto out = future.get();
      ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
      for (size_t i = 0; i < out.n_rows; ++i) {
        ASSERT_EQ(out.cells[i].tag, YBTHIN_BIND_I32);
        seen.push_back(static_cast<int32_t>(out.cells[i].i));
      }
      paging_state = std::move(out.paging_state);
      ++pages;
      ASSERT_LT(pages, 100) << "paging did not terminate";
    } while (!paging_state.empty());

    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    ASSERT_EQ(seen.size(), static_cast<size_t>(kNumRows))
        << "scan did not cover every tablet: an unset/stale partition key routes the continuation "
           "back to the first tablet, so the scan stops at that tablet's last row";
    EXPECT_EQ(seen.front(), 0);
    EXPECT_EQ(seen.back(), kNumRows - 1);
  }

  // ---- backward paged scan must also cross every tablet, in reverse --------
  // A backward scan starts at the LAST tablet, so its partition key needs the extra adjustment
  // GetPartitionKeyForBackwardScan makes; without it the scan starts at the first tablet and
  // returns only that tablet's rows.
  {
    int32_t target_ids[] = {r_id};
    ybthin_read_spec spec = {};
    spec.target_ids = target_ids;
    spec.n_targets = 1;
    spec.limit = kPageLimit;
    spec.is_forward_scan = 0;

    std::vector<int32_t> seen;
    std::vector<uint8_t> paging_state;
    int pages = 0;
    do {
      std::promise<ReadOutcome> promise;
      auto future = promise.get_future();
      ybthin_read_op op = {};
      op.table = table;
      op.spec = spec;
      op.paging_state_in = paging_state.empty() ? nullptr : paging_state.data();
      op.paging_state_in_len = paging_state.size();
      ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
      auto out = future.get();
      ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
      for (size_t i = 0; i < out.n_rows; ++i) {
        ASSERT_EQ(out.cells[i].tag, YBTHIN_BIND_I32);
        seen.push_back(static_cast<int32_t>(out.cells[i].i));
      }
      paging_state = std::move(out.paging_state);
      ++pages;
      ASSERT_LT(pages, 100) << "backward paging did not terminate";
    } while (!paging_state.empty());

    // Rows must arrive descending, and the scan must have covered the whole table.
    ASSERT_TRUE(std::is_sorted(seen.begin(), seen.end(), std::greater<int32_t>()))
        << "backward scan did not return rows in descending order";
    std::sort(seen.begin(), seen.end());
    seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
    ASSERT_EQ(seen.size(), static_cast<size_t>(kNumRows))
        << "backward scan did not cover every tablet";
    EXPECT_EQ(seen.front(), 0);
    EXPECT_EQ(seen.back(), kNumRows - 1);
  }

  // ---- fail fast: hash values on a range-sharded table ---------------------
  {
    ybthin_bind hash_values[] = {I32(1)};
    int32_t target_ids[] = {r_id};
    ybthin_read_spec spec = {};
    spec.hash_values = hash_values;
    spec.n_hash = 1;
    spec.target_ids = target_ids;
    spec.n_targets = 1;

    std::promise<ReadOutcome> promise;
    auto future = promise.get_future();
    ybthin_read_op op = {};
    op.table = table;
    op.spec = spec;
    ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
    auto out = future.get();
    ASSERT_EQ(out.code, YBTHIN_INVALID) << "expected hash_values on a range table to be rejected";
  }

  // ---- fail fast: an upsert that does not bind the whole primary key -------
  {
    ybthin_bind no_keys[] = {I32(0)};
    int32_t vid = payload_id;
    ybthin_bind val = Bytea(payload);
    ybthin_upsert_row bad = {table, no_keys, 0, &vid, &val, 1};  // n_keys == 0
    std::promise<WriteOutcome> promise;
    auto future = promise.get_future();
    ybthin_upsert_batch_async(client, &bad, 1, &OnWriteDone, &promise);
    auto out = future.get();
    ASSERT_EQ(out.code, YBTHIN_INVALID) << "expected a partial primary key to be rejected";
  }

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
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
