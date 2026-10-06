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
// Live smoke test for the Perform-based tserver client (src/yb/thin_client/yb_thin_client.h).
// Exercises the whole C ABI -- client_create -> table_open (schema check) -> upsert_batch -> paged
// read -- and cross-checks the shim's writes and reads against ordinary SQL through the same
// tserver. Most tests drive an in-process mini cluster; the TLS data path needs an
// ExternalMiniCluster, whose tservers get --certs_dir and can therefore serve SQL under encryption.

#include <algorithm>
#include <functional>
#include <tuple>
#include <utility>
#include <array>
#include <atomic>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/write.hpp>

#include "yb/thin_client/yb_thin_client.h"

#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/retryable_requests.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"

#include "yb/common/hybrid_time.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/file_system.h"
#include "yb/util/format.h"
#include "yb/util/hdr_histogram.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status_format.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_wrapper_test_base.h"

DECLARE_bool(TEST_asyncrpc_finished_set_timedout);
DECLARE_uint64(TEST_thin_client_perform_delay_ms);
DECLARE_uint64(pg_client_heartbeat_interval_ms);
DECLARE_uint64(pg_client_session_expiration_ms);
DECLARE_bool(use_libunwind_for_stack_trace_collection);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_bool(use_client_to_server_encryption);
DECLARE_bool(allow_insecure_connections);
DECLARE_bool(TEST_private_broadcast_address);
DECLARE_string(certs_dir);
DECLARE_string(TEST_public_hostname_suffix);
DECLARE_int32(TEST_delay_before_added_to_leader_ms);

METRIC_DECLARE_histogram(handler_latency_yb_tserver_ThinClientService_Perform);

namespace yb::pgwrapper {

namespace {

// A decoded cell, deep-copied so it outlives ybthin_read_result_free.
struct DecodedCell {
  ybthin_bind_tag tag = YBTHIN_BIND_NULL;
  int64_t int_value = 0;
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
        dst.int_value = src.int_value;
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

ybthin_bind I32(int32_t value) { return ybthin_bind{YBTHIN_BIND_I32, value, nullptr, 0}; }

ybthin_bind I64(int64_t value) { return ybthin_bind{YBTHIN_BIND_I64, value, nullptr, 0}; }

ybthin_bind U32(uint32_t value) {
  return ybthin_bind{YBTHIN_BIND_U32, static_cast<int64_t>(value), nullptr, 0};
}

ybthin_bind Bytea(const std::string& str) {
  return ybthin_bind{
      YBTHIN_BIND_BYTEA, 0, reinterpret_cast<const uint8_t*>(str.data()), str.size()};
}

// A (k int, v int) table keyed on (k HASH, v), for UpsertKeys and ReadKeys below.
constexpr auto kKeyTableDdl = "CREATE TABLE t (k int, v int, PRIMARY KEY((k) HASH, v))";
constexpr auto kDbOidQuery = "SELECT oid FROM pg_database WHERE datname = current_database()";
constexpr auto kKeyTableOidQuery = "SELECT 't'::regclass::oid";

struct TableOids {
  uint32_t db_oid;
  uint32_t table_oid;
};

// Converts a shim status, freeing its message.
Status ShimStatus(const char* what, ybthin_status status) {
  std::string message;
  if (status.message) {
    message = status.message;
    ybthin_string_free(status.message);
  }
  if (status.code == YBTHIN_OK) {
    return Status::OK();
  }
  return STATUS_FORMAT(
      IllegalState, "$0 failed with code $1: $2", what, static_cast<int>(status.code), message);
}

struct ThinClientDeleter {
  void operator()(ybthin_client* client) const { ybthin_client_destroy(client); }
};
using ThinClientPtr = std::unique_ptr<ybthin_client, ThinClientDeleter>;

Result<ThinClientPtr> CreateThinClient(
    const std::vector<std::string>& addrs, const ybthin_pool_opts* pool, uint32_t rpc_timeout_ms,
    uint32_t num_reactors = 0) {
  std::vector<const char*> addr_ptrs;
  for (const auto& addr : addrs) {
    addr_ptrs.push_back(addr.c_str());
  }
  ybthin_client* client = nullptr;
  auto status = ShimStatus("client_create", ybthin_client_create(
      addr_ptrs.data(), addr_ptrs.size(), /* tls= */ nullptr, pool, rpc_timeout_ms, num_reactors,
      &client));
  ThinClientPtr owned(client);
  RETURN_NOT_OK(status);
  return owned;
}

// A table opened through the shim, closed with its columns when it goes out of scope.
class ThinTable {
 public:
  ThinTable() = default;
  ThinTable(ThinTable&& rhs)
      : table_(std::exchange(rhs.table_, nullptr)), info_(std::exchange(rhs.info_, {})) {}
  ThinTable& operator=(ThinTable&&) = delete;

  ~ThinTable() {
    if (table_) {
      ybthin_columns_free(info_.columns, info_.n_columns);
      ybthin_table_close(table_);
    }
  }

  ybthin_table* get() const { return table_; }
  int32_t ColumnId(size_t idx) const { return info_.columns[idx].id; }

  static Result<ThinTable> Open(ybthin_client* client, const TableOids& oids) {
    ThinTable result;
    RETURN_NOT_OK(ShimStatus("table_open", ybthin_table_open(
        client, oids.db_oid, oids.table_oid, &result.table_, &result.info_)));
    return result;
  }

 private:
  ybthin_table* table_ = nullptr;
  ybthin_table_info info_ = {};
};

}  // namespace

class PgThinClientTest : public PgMiniTestBase {
 protected:
  // One tserver keeps the in-process cluster small enough that the plaintext and TLS cases can run
  // back-to-back in one binary without exhausting memory when initdb forks postgres.
  size_t NumTabletServers() override { return 1; }

  std::string TServerAddr() const {
    return cluster_->mini_tablet_server(0)->bound_rpc_addr_str();
  }

  Result<uint32_t> FetchOid(PGConn* conn, const std::string& query) {
    auto value = VERIFY_RESULT(conn->FetchRowAsString(query));
    return static_cast<uint32_t>(std::stoul(value));
  }

  Result<TableOids> CreateKeyTable() {
    auto conn = VERIFY_RESULT(Connect());
    RETURN_NOT_OK(conn.Execute(kKeyTableDdl));
    const auto db_oid = VERIFY_RESULT(FetchOid(&conn, kDbOidQuery));
    return TableOids{db_oid, VERIFY_RESULT(FetchOid(&conn, kKeyTableOidQuery))};
  }
};

// Drives the full C ABI end-to-end against the mini tserver -- open table, upsert, paged read --
// cross-checking the writes and reads via SQL.
TEST_F(PgThinClientTest, OpenUpsertReadPaged) {
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

  // Client_create
  const auto addr = TServerAddr();
  const char* addrs[] = {addr.c_str()};
  ybthin_client* client = nullptr;
  {
    auto st = ybthin_client_create(
        addrs, 1, /* tls= */ nullptr, /* pool= */ nullptr, /* rpc_timeout_ms= */ 60000,
        /* num_reactors= */ 0, &client);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }

  // Table_open + schema check
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

  // Upsert_batch
  const std::string payload = "hello";
  std::vector<std::array<ybthin_bind, 2>> keys(kNumRows);
  std::vector<ybthin_bind> values(kNumRows);
  std::vector<int32_t> value_ids(kNumRows, payload_id);
  std::vector<ybthin_upsert_row> rows(kNumRows);
  for (int row_idx = 0; row_idx < kNumRows; ++row_idx) {
    keys[row_idx] = {I32(kHashKey), I32(row_idx)};      // (k HASH, v RANGE) in schema order
    values[row_idx] = Bytea(payload);
    rows[row_idx] = ybthin_upsert_row{
        table, keys[row_idx].data(), 2, &value_ids[row_idx], &values[row_idx], 1,
        /* ignore_after_hybrid_time= */ 0};
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

  // Paged read
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
    for (size_t row_idx = 0; row_idx < out.n_rows; ++row_idx) {
      const DecodedCell& v_cell = out.cells[row_idx * out.n_cols + 0];
      const DecodedCell& payload_cell = out.cells[row_idx * out.n_cols + 1];
      ASSERT_EQ(v_cell.tag, YBTHIN_BIND_I32);
      EXPECT_LE(v_cell.int_value, kUpperBound);  // the range bound we scanned under
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

// An `oid` column is DocDB UINT32. The shim must open a table that has one, bind an oid past
// INT32_MAX (where a signed 32-bit stand-in would wrap negative and build a different key) as a
// range key, decode the cell back as U32, and agree with SQL on what it wrote.
TEST_F(PgThinClientTest, OidColumnRoundTrip) {
  constexpr uint32_t kSqlOid = 3000000000u;   // > INT32_MAX
  constexpr uint32_t kShimOid = 3000000001u;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE o (relnode oid, blk bigint, v int, PRIMARY KEY (relnode ASC, blk ASC))"));
  ASSERT_OK(conn.ExecuteFormat("INSERT INTO o VALUES ($0, 0, 42)", kSqlOid));

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 'o'::regclass::oid"));

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
  ASSERT_EQ(info.n_columns, 3);
  ASSERT_EQ(std::string(info.columns[0].name), "relnode");
  ASSERT_EQ(info.columns[0].kind, YBTHIN_COL_RANGE);
  ASSERT_EQ(info.columns[0].type, YBTHIN_T_U32);
  ASSERT_EQ(info.columns[1].type, YBTHIN_T_I64);
  ASSERT_EQ(info.columns[2].type, YBTHIN_T_I32);
  const int32_t relnode_id = info.columns[0].id;
  const int32_t v_id = info.columns[2].id;

  // One row by its full range key, returning (relnode, v).
  auto read_row = [&](uint32_t relnode) -> ReadOutcome {
    ybthin_bind range_values[] = {U32(relnode), I64(0)};
    int32_t target_ids[] = {relnode_id, v_id};
    ybthin_read_spec spec = {};
    spec.range_values = range_values;
    spec.n_range = 2;
    spec.target_ids = target_ids;
    spec.n_targets = 2;
    spec.limit = 10;
    spec.is_forward_scan = 1;
    std::promise<ReadOutcome> promise;
    auto future = promise.get_future();
    ybthin_read_op op = {};
    op.table = table;
    op.spec = spec;
    ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
    return future.get();
  };

  // The SQL-written row is found under a U32 key bind, and its oid decodes as a U32 cell.
  {
    auto out = read_row(kSqlOid);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
    ASSERT_EQ(out.n_rows, 1u);
    ASSERT_EQ(out.n_cols, 2u);
    EXPECT_EQ(out.cells[0].tag, YBTHIN_BIND_U32);
    EXPECT_EQ(out.cells[0].int_value, static_cast<int64_t>(kSqlOid));
    EXPECT_EQ(out.cells[1].tag, YBTHIN_BIND_I32);
    EXPECT_EQ(out.cells[1].int_value, 42);
  }

  // A shim write under a U32 key is the row SQL finds under that oid.
  {
    ybthin_bind key[] = {U32(kShimOid), I64(0)};
    ybthin_bind value = I32(7);
    int32_t value_id = v_id;
    ybthin_upsert_row row{table, key, 2, &value_id, &value, 1, /* ignore_after_hybrid_time= */ 0};
    std::promise<WriteOutcome> promise;
    auto future = promise.get_future();
    ybthin_upsert_batch_async(client, &row, 1, &OnWriteDone, &promise);
    auto out = future.get();
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }
  ASSERT_EQ(7, ASSERT_RESULT(conn.FetchRow<int32_t>(
                   Format("SELECT v FROM o WHERE relnode = $0", kShimOid))));
  ASSERT_EQ(2, ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM o")));

  // A U32 bind outside 0..2^32-1 is refused before anything is sent, not wrapped into a key.
  {
    ybthin_bind key[] = {ybthin_bind{YBTHIN_BIND_U32, -1, nullptr, 0}, I64(0)};
    ybthin_bind value = I32(0);
    int32_t value_id = v_id;
    ybthin_upsert_row row{table, key, 2, &value_id, &value, 1, /* ignore_after_hybrid_time= */ 0};
    std::promise<WriteOutcome> promise;
    auto future = promise.get_future();
    ybthin_upsert_batch_async(client, &row, 1, &OnWriteDone, &promise);
    auto out = future.get();
    ASSERT_NE(out.code, YBTHIN_OK);
    ASSERT_STR_CONTAINS(out.message, "out of range");
  }
  ASSERT_EQ(2, ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM o")));

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
}

// A paged scan must observe ONE snapshot for its whole duration: every row that existed when it
// started must come back, even if it is deleted while the scan is still paging. This commits a
// DELETE of the not-yet-scanned tail between the first and second pages, so a scan that holds its
// snapshot still returns those rows.
//
// It pages by passing only the server's paging_state back, with read_time_ht == 0 throughout -- the
// natural "just keep paging" usage. If a continuation advanced read_time_serial, the server's
// ENSURE_READ_TIME_IS_SET would pick a fresh read time per page and forward it to the tablet as an
// explicit one, which makes DocDB ignore the read time in the paging_state (pgsql_operation.cc,
// guarded by !is_explicit_request_read_time) and continue at the newer snapshot -- dropping the
// deleted rows.
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
    for (size_t row_idx = 0; row_idx < out.n_rows; ++row_idx) {
      const DecodedCell& v_cell = out.cells[row_idx * out.n_cols + 0];
      ASSERT_EQ(v_cell.tag, YBTHIN_BIND_I32);
      seen.push_back(static_cast<int32_t>(v_cell.int_value));
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

// Routing is the client's job on the Perform path (PgClientSession never derives a partition key),
// and a range table has no hash columns to derive one from, so the shim builds it from the range
// key. Splitting into 4 tablets means anything that mis-routes -- or fails to follow the paging
// state onto the next tablet -- loses rows, instead of quietly passing on a single-tablet table.
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

  // Upserts spanning every tablet
  const std::string payload = "rangeval";
  std::vector<std::array<ybthin_bind, 1>> keys(kNumRows);
  std::vector<ybthin_bind> values(kNumRows);
  std::vector<int32_t> value_ids(kNumRows, payload_id);
  std::vector<ybthin_upsert_row> rows(kNumRows);
  for (int row_idx = 0; row_idx < kNumRows; ++row_idx) {
    keys[row_idx] = {I32(row_idx)};
    values[row_idx] = Bytea(payload);
    rows[row_idx] = ybthin_upsert_row{
        table, keys[row_idx].data(), 1, &value_ids[row_idx], &values[row_idx], 1,
        /* ignore_after_hybrid_time= */ 0};
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

  // Point read: exact range key routes to one tablet
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
    EXPECT_EQ(out.cells[0].int_value, 137);
  }

  // Full paged scan must cross every tablet boundary
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
      for (size_t row_idx = 0; row_idx < out.n_rows; ++row_idx) {
        ASSERT_EQ(out.cells[row_idx].tag, YBTHIN_BIND_I32);
        seen.push_back(static_cast<int32_t>(out.cells[row_idx].int_value));
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

  // A backward scan starts at the LAST tablet, so its partition key needs the adjustment
  // GetPartitionKeyForBackwardScan makes; without it the scan returns only the first tablet's
  // rows.
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
      for (size_t row_idx = 0; row_idx < out.n_rows; ++row_idx) {
        ASSERT_EQ(out.cells[row_idx].tag, YBTHIN_BIND_I32);
        seen.push_back(static_cast<int32_t>(out.cells[row_idx].int_value));
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

  // Fail fast: hash values on a range-sharded table
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

  // Fail fast: an upsert that does not bind the whole primary key
  {
    ybthin_bind no_keys[] = {I32(0)};
    int32_t vid = payload_id;
    ybthin_bind val = Bytea(payload);
    ybthin_upsert_row bad = {table, no_keys, 0, &vid, &val, 1, 0};  // n_keys == 0
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

// A range-sharded table with a FOUR column key, (k1, k2, k3, k4). Two things:
//
// 1. A range key PREFIX must bound the scan the same way in both directions. The prefix's upper
//    bound has to cover every key under it, or a reverse scan silently returns nothing while the
//    identical forward scan returns rows.
// 2. A condition on a key column must be evaluated even when that column is not a target. DocDB
//    builds both the projection and the filter from col_refs (docdb/pgsql_operation.cc), so a
//    condition column missing from col_refs filters against a column that was never read.
TEST_F(PgThinClientTest, RangeKeyPrefixesAndKeyColumnConditions) {
  constexpr int kK1 = 1;
  constexpr int kNumK2 = 3, kNumK3 = 3, kNumK4 = 4;   // 36 rows under k1 = 1

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE k (k1 int, k2 int, k3 int, k4 int, v bytea, "
      "PRIMARY KEY (k1 ASC, k2 ASC, k3 ASC, k4 ASC)) "
      // Splitting on THREE column tuples is what makes the prefix depth matter: a 1-2 column prefix
      // spans tablets while a 3-4 column one targets exactly one.
      "SPLIT AT VALUES ((1, 1, 0), (1, 2, 0), (2, 0, 0), (2, 1, 2))"));
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO k SELECT $0, c2, c3, c4, '\\xfeed'::bytea "
      "FROM generate_series(0, $1) c2, generate_series(0, $2) c3, generate_series(0, $3) c4",
      kK1, kNumK2 - 1, kNumK3 - 1, kNumK4 - 1));
  // A second value of `k1`, so a prefix scan that ignores its bounds would over-return.
  ASSERT_OK(conn.ExecuteFormat(
      "INSERT INTO k SELECT $0, c2, c3, c4, '\\xfeed'::bytea "
      "FROM generate_series(0, $1) c2, generate_series(0, $2) c3, generate_series(0, $3) c4",
      kK1 + 1, kNumK2 - 1, kNumK3 - 1, kNumK4 - 1));

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 'k'::regclass::oid"));

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
  ASSERT_EQ(info.n_columns, 5);
  const int32_t k2_id = info.columns[1].id;
  const int32_t k3_id = info.columns[2].id, k4_id = info.columns[3].id;
  ASSERT_EQ(info.columns[2].kind, YBTHIN_COL_RANGE);
  ASSERT_EQ(info.columns[3].kind, YBTHIN_COL_RANGE);

  // Pages a scan to completion and returns the values of the single target column.
  auto scan = [&](const ybthin_read_spec& spec) -> Result<std::vector<int32_t>> {
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
      SCHECK_EQ(out.code, YBTHIN_OK, IllegalState, out.message);
      for (size_t row_idx = 0; row_idx < out.n_rows; ++row_idx) {
        seen.push_back(static_cast<int32_t>(out.cells[row_idx * out.n_cols].int_value));
      }
      paging_state = std::move(out.paging_state);
      SCHECK_LT(++pages, 100, IllegalState, "paging did not terminate");
    } while (!paging_state.empty());
    return seen;
  };

  // (1) a range key prefix must behave identically forward and reverse
  // Prefix lengths 1..4: (k1), (k1,k2), (k1,k2,k3), (k1,k2,k3,k4).
  const ybthin_bind prefix[] = {I32(kK1), I32(1), I32(2), I32(3)};
  const int expected_rows[] = {kNumK2 * kNumK3 * kNumK4, kNumK3 * kNumK4, kNumK4, 1};
  int32_t target_ids[] = {k4_id};
  for (size_t prefix_len = 1; prefix_len <= 4; ++prefix_len) {
    ybthin_read_spec spec = {};
    spec.range_values = prefix;
    spec.n_range = prefix_len;
    spec.target_ids = target_ids;
    spec.n_targets = 1;

    spec.is_forward_scan = 1;
    auto fwd = ASSERT_RESULT(scan(spec));
    spec.is_forward_scan = 0;
    auto rev = ASSERT_RESULT(scan(spec));

    ASSERT_EQ(fwd.size(), static_cast<size_t>(expected_rows[prefix_len - 1]))
        << "forward scan on a " << prefix_len << "-column range prefix";
    ASSERT_EQ(rev.size(), fwd.size())
        << "reverse scan on a " << prefix_len << "-column range prefix returned " << rev.size()
        << " rows but forward returned " << fwd.size();
    std::sort(fwd.begin(), fwd.end());
    std::sort(rev.begin(), rev.end());
    ASSERT_EQ(fwd, rev) << "forward and reverse disagree on a " << prefix_len << "-column prefix";
  }

  // (2) a condition on a key column that is NOT a target
  // k3 is the 3rd key column, k4 the 4th; neither is the target here, so both rely on the
  // condition column reaching col_refs.
  int32_t only_k2[] = {k2_id};
  for (const auto& [cond_col, cond_val, expected, name] :
       std::vector<std::tuple<int32_t, int32_t, int, const char*>>{
           {k3_id, 1, kNumK2 * kNumK4, "k3 (3rd key column)"},
           {k4_id, 2, kNumK2 * kNumK3, "k4 (4th key column)"}}) {
    ybthin_bind k1_only[] = {I32(kK1)};
    ybthin_cond conds[] = {{cond_col, YBTHIN_EQ, I32(cond_val)}};
    ybthin_read_spec spec = {};
    spec.range_values = k1_only;
    spec.n_range = 1;
    spec.conds = conds;
    spec.n_conds = 1;
    spec.target_ids = only_k2;
    spec.n_targets = 1;

    spec.is_forward_scan = 1;
    auto fwd = ASSERT_RESULT(scan(spec));
    spec.is_forward_scan = 0;
    auto rev = ASSERT_RESULT(scan(spec));
    ASSERT_EQ(fwd.size(), static_cast<size_t>(expected)) << "forward Eq cond on " << name;
    ASSERT_EQ(rev.size(), static_cast<size_t>(expected)) << "reverse Eq cond on " << name;
  }

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
}

// Mirrors the shape of a table migrated from hash to range partitioning: a mixed-type four column
// range key split on 3-column tuples. Crucially it reads a SINGLE page with limit 1 rather than
// paging to exhaustion, the way such a caller issues a "covering row" lookup -- a reverse scan
// started on the wrong tablet hands back an empty first page, which the caller sees as "no rows".
TEST_F(PgThinClientTest, RangeShardedDescendingPrefixSinglePage) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE range4 ("
      "  k1 TEXT NOT NULL, k2 SMALLINT NOT NULL, k3 BIGINT NOT NULL,"
      "  k4 BIGINT NOT NULL, v1 INTEGER NOT NULL, v2 BYTEA NOT NULL,"
      "  PRIMARY KEY (k1 ASC, k2 ASC, k3 ASC, k4 ASC))"
      " SPLIT AT VALUES (('b', 0, 2), ('b', 0, 4))"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO range4 "
      "SELECT 'b', 0, c3, c4*100, 100, '\\x02'::bytea "
      "FROM generate_series(1,5) c3, generate_series(0,3) c4"));
  // Bracket rows, so a read that loses its bounds is caught rather than passing.
  ASSERT_OK(conn.Execute(
      "INSERT INTO range4 VALUES "
      "('a', 0, 9, 0, 100, '\\x0a'::bytea), ('c', 0, 9, 0, 100, '\\x0c'::bytea)"));

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(
      FetchOid(&conn, "SELECT 'range4'::regclass::oid"));

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
  ASSERT_EQ(info.n_columns, 6);
  for (int key_idx = 0; key_idx < 4; ++key_idx) {
    ASSERT_EQ(info.columns[key_idx].kind, YBTHIN_COL_RANGE) << "key column " << key_idx;
  }
  const int32_t k3_id = info.columns[2].id, k4_id = info.columns[3].id;

  const std::string k1_value = "b";
  auto text_k1 = [&] {
    return ybthin_bind{YBTHIN_BIND_TEXT, 0,
                       reinterpret_cast<const uint8_t*>(k1_value.data()), k1_value.size()};
  };
  auto i16 = [](int64_t value) { return ybthin_bind{YBTHIN_BIND_I16, value, nullptr, 0}; };
  auto i64 = [](int64_t value) { return ybthin_bind{YBTHIN_BIND_I64, value, nullptr, 0}; };

  int32_t targets[] = {k3_id, k4_id};
  // ONE page only -- no paging loop to paper over an empty first page.
  auto first_page = [&](const ybthin_read_spec& spec) -> Result<std::vector<std::pair<int64_t,
                                                                                      int64_t>>> {
    std::promise<ReadOutcome> promise;
    auto future = promise.get_future();
    ybthin_read_op op = {};
    op.table = table;
    op.spec = spec;
    ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
    auto out = future.get();
    SCHECK_EQ(out.code, YBTHIN_OK, IllegalState, out.message);
    std::vector<std::pair<int64_t, int64_t>> rows;
    for (size_t row_idx = 0; row_idx < out.n_rows; ++row_idx) {
      rows.emplace_back(out.cells[row_idx * out.n_cols].int_value,
                        out.cells[row_idx * out.n_cols + 1].int_value);
    }
    return rows;
  };

  // Descending, limit 1, one page, at every prefix depth.
  const ybthin_bind prefix[] = {text_k1(), i16(0), i64(3), i64(200)};
  struct Probe { size_t prefix_len; int64_t want_k3, want_k4; const char* id; };
  for (const auto& probe : {Probe{1, 5, 300, "depth 1 ['b']"},
                            Probe{2, 5, 300, "depth 2 ['b',0]"},
                            Probe{3, 3, 300, "depth 3 ['b',0,3]"},
                            Probe{4, 3, 200, "depth 4 ['b',0,3,200] (full key)"}}) {
    ybthin_read_spec spec = {};
    spec.range_values = prefix;
    spec.n_range = probe.prefix_len;
    spec.target_ids = targets;
    spec.n_targets = 2;
    spec.limit = 1;
    spec.is_forward_scan = 0;
    auto rows = ASSERT_RESULT(first_page(spec));
    ASSERT_EQ(rows.size(), 1u) << probe.id << ": descending limit 1 returned " << rows.size()
                               << " rows on the first page";
    EXPECT_EQ(rows[0].first, probe.want_k3) << probe.id;
    EXPECT_EQ(rows[0].second, probe.want_k4) << probe.id;
  }

  // An Eq/Lt cond on k3, the key column right after the prefix.
  const ybthin_bind pfx2[] = {text_k1(), i16(0)};
  {
    ybthin_cond eq[] = {{k3_id, YBTHIN_EQ, i64(3)}};
    ybthin_read_spec spec = {};
    spec.range_values = pfx2;
    spec.n_range = 2;
    spec.conds = eq;
    spec.n_conds = 1;
    spec.target_ids = targets;
    spec.n_targets = 2;
    spec.limit = 10;
    spec.is_forward_scan = 1;
    auto fwd = ASSERT_RESULT(first_page(spec));
    ASSERT_EQ(fwd.size(), 4u) << "forward Eq cond on k3 (3rd key column)";

    spec.limit = 1;
    spec.is_forward_scan = 0;
    auto rev = ASSERT_RESULT(first_page(spec));
    ASSERT_EQ(rev.size(), 1u) << "descending Eq cond on k3";
    EXPECT_EQ(rev[0].first, 3);
    EXPECT_EQ(rev[0].second, 300);
  }
  {
    // Cond on the LAST key column already works; lock it in.
    ybthin_cond lt[] = {{k4_id, YBTHIN_LT, i64(250)}};
    ybthin_read_spec spec = {};
    spec.range_values = pfx2;
    spec.n_range = 2;
    spec.conds = lt;
    spec.n_conds = 1;
    spec.target_ids = targets;
    spec.n_targets = 2;
    spec.limit = 1;
    spec.is_forward_scan = 0;
    auto rows = ASSERT_RESULT(first_page(spec));
    ASSERT_EQ(rows.size(), 1u) << "descending cond on k4";
    EXPECT_EQ(rows[0].first, 5);
    EXPECT_EQ(rows[0].second, 200);
  }

  // A short FIRST page is only a tablet boundary if the scan then continues, so page `k3 < 4` out
  // and count. Lt also gets rows through where an Eq folded into the prefix would not.
  {
    ybthin_cond lt[] = {{k3_id, YBTHIN_LT, i64(4)}};
    ybthin_read_spec spec = {};
    spec.range_values = pfx2;
    spec.n_range = 2;
    spec.conds = lt;
    spec.n_conds = 1;
    spec.target_ids = targets;
    spec.n_targets = 2;
    spec.limit = 10;
    spec.is_forward_scan = 1;

    std::vector<std::pair<int64_t, int64_t>> all;
    std::vector<uint8_t> ps;
    int pages = 0;
    do {
      std::promise<ReadOutcome> promise;
      auto future = promise.get_future();
      ybthin_read_op op = {};
      op.table = table;
      op.spec = spec;
      op.paging_state_in = ps.empty() ? nullptr : ps.data();
      op.paging_state_in_len = ps.size();
      ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
      auto out = future.get();
      ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
      for (size_t row_idx = 0; row_idx < out.n_rows; ++row_idx) {
        all.emplace_back(out.cells[row_idx * out.n_cols].int_value,
                         out.cells[row_idx * out.n_cols + 1].int_value);
      }
      ps = std::move(out.paging_state);
      ASSERT_LT(++pages, 20) << "L7 paging did not terminate";
    } while (!ps.empty());

    // k3 1,2,3 x k4 0,100,200,300, and nothing from k1 'a' or 'c'.
    ASSERT_EQ(all.size(), 12u)
        << "`k3 < 4` paged to exhaustion returned " << all.size() << " rows over " << pages
        << " pages; a short FIRST page is only a tablet boundary if the scan then continues";
    for (const auto& [k3, k4] : all) {
      EXPECT_LT(k3, 4);
      EXPECT_GE(k3, 1);
    }
  }

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
}

// Range-partitioning wider tables needs prefixes 7-8 columns deep, past the 4 columns the other
// tests cover. Nothing in the routing is depth-limited, so this pins that down. Also asserts the
// null-key guard, since DocDB forbids nulls in the range key prefix.
TEST_F(PgThinClientTest, DeepRangeKeyPrefixes) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE deep (k1 TEXT, k2 SMALLINT, k3 BIGINT, k4 BIGINT, k5 INT, k6 INT, k7 INT,"
      "  k8 INT, v BYTEA, PRIMARY KEY (k1 ASC, k2 ASC, k3 ASC, k4 ASC, k5 ASC, k6 ASC, k7 ASC,"
      "  k8 ASC)) SPLIT AT VALUES (('b', 0, 2), ('b', 0, 4))"));
  // k3 varies 1..5 across the split points, k8 varies 0..3 within each; k4..k7 are fixed at 7 so a
  // deep prefix has to carry them to reach a row.
  ASSERT_OK(conn.Execute(
      "INSERT INTO deep SELECT 'b', 0, s, 7, 7, 7, 7, o, '\\x07'::bytea "
      "FROM generate_series(1,5) s, generate_series(0,3) o"));
  ASSERT_OK(conn.Execute(
      "INSERT INTO deep VALUES ('a',0,9,7,7,7,7,0,'\\x0a'::bytea),"
      " ('c',0,9,7,7,7,7,0,'\\x0c'::bytea)"));

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 'deep'::regclass::oid"));
  const auto addr = TServerAddr();
  const char* addrs[] = {addr.c_str()};
  ybthin_client* client = nullptr;
  {
    auto st = ybthin_client_create(
        addrs, 1, nullptr, nullptr, 60000, 0, &client);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }
  ybthin_table* table = nullptr;
  ybthin_table_info info = {};
  {
    auto st = ybthin_table_open(client, db_oid, table_oid, &table, &info);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }
  ASSERT_EQ(info.n_columns, 9);
  for (int key_idx = 0; key_idx < 8; ++key_idx) {
    ASSERT_EQ(info.columns[key_idx].kind, YBTHIN_COL_RANGE) << "key column " << key_idx;
  }
  const int32_t k3_id = info.columns[2].id, k8_id = info.columns[7].id;

  const std::string k1_value = "b";
  const ybthin_bind prefix[] = {
      {YBTHIN_BIND_TEXT, 0, reinterpret_cast<const uint8_t*>(k1_value.data()), k1_value.size()},
      {YBTHIN_BIND_I16, 0, nullptr, 0},   // k2 = 0
      {YBTHIN_BIND_I64, 3, nullptr, 0},   // k3 = 3
      {YBTHIN_BIND_I64, 7, nullptr, 0},   // k4
      {YBTHIN_BIND_I32, 7, nullptr, 0},   // k5
      {YBTHIN_BIND_I32, 7, nullptr, 0},   // k6
      {YBTHIN_BIND_I32, 7, nullptr, 0},   // k7
      {YBTHIN_BIND_I32, 2, nullptr, 0}};  // k8 = 2 -> the full key
  int32_t targets[] = {k3_id, k8_id};

  auto one_page = [&](size_t n_range, int forward) -> Result<size_t> {
    ybthin_read_spec spec = {};
    spec.range_values = prefix;
    spec.n_range = n_range;
    spec.target_ids = targets;
    spec.n_targets = 2;
    spec.limit = 10;
    spec.is_forward_scan = forward;
    std::promise<ReadOutcome> promise;
    auto future = promise.get_future();
    ybthin_read_op op = {};
    op.table = table;
    op.spec = spec;
    ybthin_read_async(client, &op, 1, 0, &OnReadDone, &promise);
    auto out = future.get();
    SCHECK_EQ(out.code, YBTHIN_OK, IllegalState, out.message);
    return out.n_rows;
  };

  // Depths 3..8 all address the same 4 rows (k3=3, k8=0..3) until the full key narrows to 1.
  for (size_t prefix_len = 3; prefix_len <= 8; ++prefix_len) {
    const size_t want = (prefix_len == 8) ? 1 : 4;
    auto fwd = ASSERT_RESULT(one_page(prefix_len, 1));
    auto rev = ASSERT_RESULT(one_page(prefix_len, 0));
    ASSERT_EQ(fwd, want) << "forward, " << prefix_len << "-column prefix";
    ASSERT_EQ(rev, want) << "reverse, " << prefix_len << "-column prefix returned " << rev
                         << " on the first page";
  }

  // Null in the key prefix is rejected, not bound.
  {
    ybthin_bind with_null[] = {prefix[0], {YBTHIN_BIND_NULL, 0, nullptr, 0}};
    ybthin_read_spec spec = {};
    spec.range_values = with_null;
    spec.n_range = 2;
    spec.target_ids = targets;
    spec.n_targets = 2;
    std::promise<ReadOutcome> promise;
    auto future = promise.get_future();
    ybthin_read_op op = {};
    op.table = table;
    op.spec = spec;
    ybthin_read_async(client, &op, 1, 0, &OnReadDone, &promise);
    auto out = future.get();
    ASSERT_EQ(out.code, YBTHIN_INVALID) << "a null range key value must be rejected";
  }

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
}

// A scan must STOP at the end of its key range, not at the end of the table.
//
// A range key prefix fixes where a scan starts. If nothing fixes where it ends, the scan runs from
// its start key to the physical end of the table in whichever direction it scans, crossing every
// remaining tablet and handing back an empty page plus a continuation for each. Results stay
// correct -- DocDB filters per tablet -- so this is invisible to a caller that only checks rows,
// but a caller that pages to exhaustion pays one round trip per remaining tablet, and one that caps
// consecutive empty pages eventually trips its own guard and reports the scan as wedged.
//
// The table below puts each bucket in its own tablet, so EVERY key range read here lives in exactly
// one tablet and every scan must finish in a single page. Page counts, not row counts, are the
// assertion: a scan that refuses to end shows up as a page count that tracks the number of tablets
// ahead of it, in whichever direction it scans.
TEST_F(PgThinClientTest, RangeScanStopsAtTheEndOfItsKeyRange) {
  constexpr int kNumBuckets = 6;

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE range4_asc ("
      "  k1 SMALLINT NOT NULL, k2 TEXT NOT NULL, k3 BIGINT NOT NULL,"
      "  k4 BIGINT NOT NULL, v1 INTEGER NOT NULL, v2 BYTEA NOT NULL,"
      "  PRIMARY KEY (k1 ASC, k2 ASC, k3 ASC, k4 ASC))"
      " SPLIT AT VALUES ((1), (2), (3), (4), (5))"));
  // The same shape with the leading key column DESCENDING. A prefix's bounds are padded with
  // kLowest/kHighest, which are markers in ENCODED key order -- so they must land the same way
  // whichever direction a column sorts. Callers do key on a DESC column (see the is_forward_scan
  // note in yb_thin_client.h), and the split points run high-to-low to match the key order.
  ASSERT_OK(conn.Execute(
      "CREATE TABLE range4_desc ("
      "  k1 SMALLINT NOT NULL, k2 TEXT NOT NULL, k3 BIGINT NOT NULL,"
      "  k4 BIGINT NOT NULL, v1 INTEGER NOT NULL, v2 BYTEA NOT NULL,"
      "  PRIMARY KEY (k1 DESC, k2 ASC, k3 ASC, k4 ASC))"
      " SPLIT AT VALUES ((4), (3), (2), (1), (0))"));
  // One row per bucket, all at the same (branch, k3, k4) -- so a bucket's whole key
  // range, at any prefix depth, is one row in one tablet.
  for (const char* t : {"range4_asc", "range4_desc"}) {
    ASSERT_OK(conn.ExecuteFormat(
        "INSERT INTO $0 SELECT b, 'br', 7, 0, 100, '\\xfeed'::bytea "
        "FROM generate_series(0, $1) b", t, kNumBuckets - 1));
  }

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 'range4_asc'::regclass::oid"));
  const auto desc_table_oid = ASSERT_RESULT(
      FetchOid(&conn, "SELECT 'range4_desc'::regclass::oid"));

  const auto addr = TServerAddr();
  const char* addrs[] = {addr.c_str()};
  ybthin_client* client = nullptr;
  {
    auto st = ybthin_client_create(
        addrs, 1, /* tls= */ nullptr, /* pool= */ nullptr, /* rpc_timeout_ms= */ 60000,
        /* num_reactors= */ 0, &client);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }
  const std::string branch = "br";
  auto text_br = [&] {
    return ybthin_bind{YBTHIN_BIND_TEXT, 0,
                       reinterpret_cast<const uint8_t*>(branch.data()), branch.size()};
  };
  auto i16 = [](int64_t v) { return ybthin_bind{YBTHIN_BIND_I16, v, nullptr, 0}; };
  auto i64 = [](int64_t v) { return ybthin_bind{YBTHIN_BIND_I64, v, nullptr, 0}; };

  // Every assertion below is about page counts, so it holds for either sort direction: each bucket
  // owns a tablet in both tables, only their order on disk differs.
  auto check_table = [&](uint32_t oid, const char* label) {
    SCOPED_TRACE(label);
    ybthin_table* table = nullptr;
    ybthin_table_info info = {};
    {
      auto st = ybthin_table_open(client, db_oid, oid, &table, &info);
      ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
    }
    ASSERT_EQ(info.n_columns, 6);
    const int32_t segno_id = info.columns[2].id, chunk_id = info.columns[3].id;
    int32_t targets[] = {segno_id, chunk_id};

    // Pages a scan by hand and reports (pages, rows). Paging by hand rather than asserting on rows
    // is the point: a scan that never ends is a page count, not a hang.
    struct Paged { int pages; size_t rows; };
    auto page_out = [&](const ybthin_read_spec& spec) -> Result<Paged> {
      Paged out_counts = {0, 0};
      std::vector<uint8_t> ps;
      do {
        std::promise<ReadOutcome> promise;
        auto future = promise.get_future();
        ybthin_read_op op = {};
        op.table = table;
        op.spec = spec;
        op.paging_state_in = ps.empty() ? nullptr : ps.data();
        op.paging_state_in_len = ps.size();
        ybthin_read_async(client, &op, 1, /* read_time_ht= */ 0, &OnReadDone, &promise);
        auto out = future.get();
        SCHECK_EQ(out.code, YBTHIN_OK, IllegalState, out.message);
        out_counts.rows += out.n_rows;
        ps = std::move(out.paging_state);
        SCHECK_LT(++out_counts.pages, 100, IllegalState, "paging did not terminate");
      } while (!ps.empty());
      return out_counts;
    };

    // The safekeeper's covering arm, one per bucket: a (k1, k2, k3) prefix scanned
    // descending, limit 1. Unbounded, bucket B walks B tablets down to tablet 0 -- so the failure
    // grows with the bucket, and the deepest bucket is the one that trips a caller's empty-page
    // cap.
    // The forward arm is the mirror image: it walks the tablets above B.
    for (int bucket = 0; bucket < kNumBuckets; ++bucket) {
      const ybthin_bind prefix[] = {i16(bucket), text_br(), i64(7), i64(0)};
      for (size_t n_range = 1; n_range <= 4; ++n_range) {
        for (int forward = 0; forward <= 1; ++forward) {
          ybthin_read_spec spec = {};
          spec.range_values = prefix;
          spec.n_range = n_range;
          spec.target_ids = targets;
          spec.n_targets = 2;
          spec.limit = 1;
          spec.is_forward_scan = forward;
          auto paged = ASSERT_RESULT(page_out(spec));
          ASSERT_EQ(paged.rows, 1u)
              << "bucket " << bucket << ", " << n_range << "-column prefix, "
              << (forward ? "ascending" : "descending");
          ASSERT_EQ(paged.pages, 1)
              << "bucket " << bucket << ", " << n_range << "-column prefix, "
              << (forward ? "ascending" : "descending") << ": took " << paged.pages
              << " pages; this key range lives in a single tablet, so the scan walked past its end";
        }
      }
    }

    // Same, with the prefix's last column supplied as an Eq cond instead: the shim folds it into
    // the prefix, so the bounds must be derived AFTER that fold or the range stays a tablet wide.
    for (int bucket = 0; bucket < kNumBuckets; ++bucket) {
      const ybthin_bind prefix[] = {i16(bucket), text_br()};
      ybthin_cond eq[] = {{segno_id, YBTHIN_EQ, i64(7)}};
      ybthin_read_spec spec = {};
      spec.range_values = prefix;
      spec.n_range = 2;
      spec.conds = eq;
      spec.n_conds = 1;
      spec.target_ids = targets;
      spec.n_targets = 2;
      spec.limit = 1;
      spec.is_forward_scan = 0;
      auto paged = ASSERT_RESULT(page_out(spec));
      ASSERT_EQ(paged.rows, 1u) << "bucket " << bucket << ", Eq on k3, descending";
      ASSERT_EQ(paged.pages, 1) << "bucket " << bucket << ", Eq on k3, descending: took "
                                << paged.pages << " pages";
    }

    // An UNBOUNDED scan still has to cross every tablet -- the fix must not truncate a full scan.
    for (int forward = 0; forward <= 1; ++forward) {
      ybthin_read_spec spec = {};
      spec.target_ids = targets;
      spec.n_targets = 2;
      spec.limit = 1;
      spec.is_forward_scan = forward;
      auto paged = ASSERT_RESULT(page_out(spec));
      ASSERT_EQ(paged.rows, static_cast<size_t>(kNumBuckets))
          << "unbounded " << (forward ? "ascending" : "descending") << " scan dropped rows";
    }

    ybthin_columns_free(info.columns, info.n_columns);
    ybthin_table_close(table);
  };

  check_table(table_oid, "k1 ASC");
  check_table(desc_table_oid, "k1 DESC");

  ybthin_client_destroy(client);
}

// A write the server has ALREADY replicated must report success, not failure.
//
// DocDB dedupes writes on (client_id, request_id) and rejects a replay of an id it has already
// replicated: "Duplicate request N from client C" (consensus/retryable_requests.cc). An id only
// reaches that set by going through Raft, so the verdict means the write is DURABLE. For the
// idempotent upserts this ABI can express, that is the caller's success condition.
//
// Reported from production: a caller whose upsert drew that verdict treated it as fatal, dropped
// the connection, reconnected, replayed byte-identically, drew the identical verdict, and looped
// roughly once a second indefinitely -- 8340 rejections against one request id with no backoff.
// The reply is deterministic, so no amount of retrying or waiting can clear it; only reading it
// correctly can. Note that upsert-mode idempotence does NOT save this: retryable_requests sits
// above DocDB's row layer and rejects the replay before it can become a no-op write.
//
// TEST_asyncrpc_finished_set_timedout reproduces that shape exactly -- the write replicates, the
// client is told it timed out, and the resend reuses the same retryable request id (async_rpc.cc:
// "we are trying to resend all ops from this RPC and need to reuse retryable request ID"), so the
// tablet rejects the resend as a duplicate.
TEST_F(PgThinClientTest, AlreadyReplicatedWriteReportsSuccess) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE dup_w (k int, v bytea, PRIMARY KEY(k ASC))"));

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 'dup_w'::regclass::oid"));

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

  const std::string payload = "already-there";
  ybthin_bind key[] = {I32(7)};
  ybthin_bind value[] = {Bytea(payload)};
  int32_t value_ids[] = {v_id};
  ybthin_upsert_row row = {table, key, 1, value_ids, value, 1, /* no fence */ 0};

  std::promise<WriteOutcome> promise;
  auto future = promise.get_future();
  // Replicate the write, then make the client believe it timed out so it resends the identical
  // ops -- and with them the identical retryable request id.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = true;
  ybthin_upsert_batch_async(client, &row, 1, &OnWriteDone, &promise);
  SleepFor(3s);
  // Let the resend through: it now carries an id the tablet has already replicated, so the tablet
  // answers AlreadyPresent. That is the verdict under test.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = false;

  auto out = future.get();
  ASSERT_EQ(out.code, YBTHIN_OK)
      << "an already-replicated write must report success, got: " << out.message;

  // ...and the success report must be truthful: the row is really there, exactly once.
  ASSERT_EQ(1, ASSERT_RESULT(conn.FetchRow<PGUint64>("SELECT count(*) FROM dup_w WHERE k = 7")));
  ASSERT_EQ(payload, ASSERT_RESULT(conn.FetchRow<std::string>(
                         "SELECT encode(v, 'escape') FROM dup_w WHERE k = 7")));

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
}

// ybthin_client_create must switch stack trace collection to libunwind (#33916). Two clients in one
// process, so a write that fires only once per process cannot pass.
TEST_F(
    PgThinClientTest, YB_DISABLE_TEST_IN_SANITIZERS(ClientCreateSelectsLibunwindForStackTraces)) {
  const auto addr = TServerAddr();
  const char* addrs[] = {addr.c_str()};

  for (int attempt = 0; attempt < 2; ++attempt) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_libunwind_for_stack_trace_collection) = false;
    ybthin_client* client = nullptr;
    auto st = ybthin_client_create(
        addrs, /* n_addrs= */ 1, /* tls= */ nullptr, /* pool= */ nullptr,
        /* rpc_timeout_ms= */ 60000, /* num_reactors= */ 0, &client);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
    ASSERT_TRUE(FLAGS_use_libunwind_for_stack_trace_collection) << "attempt " << attempt;
    ybthin_client_destroy(client);
  }
}

// A fenced write that already replicated must still report success when the client's resend of the
// same retryable request id arrives after the fence has passed. The already-replicated verdict is
// about the FIRST attempt, which committed inside its fence; reporting the resend as YBTHIN_FENCED
// ("did NOT take effect") would tell an incoming lease holder the row is absent while it is
// durably present -- the exact inversion the fence exists to prevent. This is why RaftConsensus
// consults the retryable-request registry before the fence.
TEST_F(PgThinClientTest, ReplayOfReplicatedWriteWinsOverExpiredFence) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute(
      "CREATE TABLE dup_fenced (k int, v bytea, PRIMARY KEY(k ASC))"));

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 'dup_fenced'::regclass::oid"));

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

  // Wide enough that the first attempt is comfortably admitted even on a slow build, short enough
  // that the resend below arrives after it has passed.
  constexpr int kFenceDelaySec = 10;
  const auto fence = HybridTime::FromMicros(
      static_cast<uint64_t>(GetCurrentTimeMicros()) + kFenceDelaySec * 1000000ULL).ToPB();

  const std::string payload = "fenced-dup";
  ybthin_bind key[] = {I32(11)};
  ybthin_bind value[] = {Bytea(payload)};
  int32_t value_ids[] = {v_id};
  ybthin_upsert_row row = {table, key, 1, value_ids, value, 1, fence};

  std::promise<WriteOutcome> promise;
  auto future = promise.get_future();
  // Replicate the write inside its fence, then make the client believe it timed out so it keeps
  // resending the identical ops -- same retryable request id, same fence.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = true;
  ybthin_upsert_batch_async(client, &row, 1, &OnWriteDone, &promise);
  // Hold the flag until the fence is well past, so the resend that gets through carries an
  // expired fence for an id the tablet has already replicated.
  SleepFor((kFenceDelaySec + 5) * 1s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_asyncrpc_finished_set_timedout) = false;

  auto out = future.get();
  ASSERT_EQ(out.code, YBTHIN_OK)
      << "a replayed already-replicated write must report success even though its fence has "
      << "since passed; got: " << out.message;

  ASSERT_EQ(1, ASSERT_RESULT(conn.FetchRow<PGUint64>(
                   "SELECT count(*) FROM dup_fenced WHERE k = 11")));
  ASSERT_EQ(payload, ASSERT_RESULT(conn.FetchRow<std::string>(
                         "SELECT encode(v, 'escape') FROM dup_fenced WHERE k = 11")));

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
}

// A write whose fence has passed must be rejected AND must not take effect -- hence the assertions
// on table contents, not just the status code. Also checks the rejection leaves no
// retryable-request registration behind -- the rejection runs after registration (see
// ReplayOfReplicatedWriteWinsOverExpiredFence for why) and must undo it.
TEST_F(PgThinClientTest, WriteFencedByIgnoreAfterHybridTime) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE fenced (k int, v bytea, PRIMARY KEY(k ASC))"));

  const auto db_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT oid FROM pg_database "
                                                     "WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOid(&conn, "SELECT 'fenced'::regclass::oid"));

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

  const std::string payload = "fence";
  int32_t value_ids[] = {v_id};

  auto upsert = [&](int32_t k, uint64_t fence) -> ybthin_status_code {
    ybthin_bind key[] = {I32(k)};
    ybthin_bind value[] = {Bytea(payload)};
    ybthin_upsert_row row = {table, key, 1, value_ids, value, 1, fence};
    std::promise<WriteOutcome> promise;
    auto future = promise.get_future();
    ybthin_upsert_batch_async(client, &row, 1, &OnWriteDone, &promise);
    return future.get().code;
  };

  // HybridTime 1 is behind any clock reading the leader can take.
  ASSERT_EQ(upsert(1, 1), YBTHIN_FENCED) << "a write past its fence must be rejected";
  ASSERT_EQ(0, ASSERT_RESULT(conn.FetchRow<PGUint64>(
                   "SELECT count(*) FROM fenced WHERE k = 1")))
      << "a fenced write must not take effect";

  // Compared against the leader's clock, so it has to be a hybrid time, not a micro count.
  const auto far_future = HybridTime::FromMicros(
      static_cast<uint64_t>(GetCurrentTimeMicros()) + 3600 * 1000000ULL).ToPB();
  ASSERT_EQ(upsert(2, far_future), YBTHIN_OK) << "a write inside its fence must be applied";
  ASSERT_EQ(1, ASSERT_RESULT(conn.FetchRow<PGUint64>(
                   "SELECT count(*) FROM fenced WHERE k = 2")));

  // No fence at all behaves as before.
  ASSERT_EQ(upsert(3, 0), YBTHIN_OK);
  ASSERT_EQ(1, ASSERT_RESULT(conn.FetchRow<PGUint64>(
                   "SELECT count(*) FROM fenced WHERE k = 3")));

  // The fence is judged against the hybrid time the op is assigned, not an earlier clock reading:
  // a write admitted inside its fence that stalls until the fence has passed before its hybrid
  // time is chosen must still be rejected.
  constexpr int kNearFenceMs = 500;
  const auto near_future = HybridTime::FromMicros(
      static_cast<uint64_t>(GetCurrentTimeMicros()) + kNearFenceMs * 1000ULL).ToPB();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_before_added_to_leader_ms) = 4 * kNearFenceMs;
  const auto stalled = upsert(4, near_future);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_delay_before_added_to_leader_ms) = 0;
  ASSERT_EQ(stalled, YBTHIN_FENCED)
      << "a write whose fence passes before its hybrid time is chosen must be rejected";
  ASSERT_EQ(0, ASSERT_RESULT(conn.FetchRow<PGUint64>(
                   "SELECT count(*) FROM fenced WHERE k = 4")))
      << "a fenced write must not take effect";

  // A leaked entry from a fenced round never drains, so this would never hold. Waiting rather
  // than sampling absorbs the cluster's unrelated background writes.
  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> {
        for (const auto& peer : ListTabletPeers(cluster_.get(), ListPeersFilter::kAll)) {
          auto raft_consensus = VERIFY_RESULT(peer->GetRaftConsensus());
          if (raft_consensus->TEST_CountRetryableRequests().running != 0) {
            return false;
          }
        }
        return true;
      },
      10s, "fenced write left a retryable-request registration behind"));

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
}

namespace {

// Upserts rows (hash_key, v) for v in [from, to) into a (k int, v int) table keyed on (k HASH, v).
WriteOutcome UpsertKeys(
    ybthin_client* client, ybthin_table* table, int hash_key, int from, int to,
    uint64_t ignore_after_hybrid_time = 0) {
  std::vector<std::array<ybthin_bind, 2>> keys;
  for (int v = from; v < to; ++v) {
    keys.push_back({I32(hash_key), I32(v)});
  }
  std::vector<ybthin_upsert_row> rows;
  for (auto& key : keys) {
    rows.push_back(
        ybthin_upsert_row{table, key.data(), 2, nullptr, nullptr, 0, ignore_after_hybrid_time});
  }
  std::promise<WriteOutcome> promise;
  auto future = promise.get_future();
  ybthin_upsert_batch_async(client, rows.data(), rows.size(), &OnWriteDone, &promise);
  return future.get();
}

// Retries the upsert, as a caller does, until it lands.
Status UpsertKeysWithRetries(
    ybthin_client* client, ybthin_table* table, int hash_key, int from, int to,
    MonoDelta timeout) {
  return WaitFor(
      [&]() -> Result<bool> {
        auto out = UpsertKeys(client, table, hash_key, from, to);
        if (out.code != YBTHIN_OK) {
          LOG(INFO) << "Upsert failed with code " << out.code << ": " << out.message;
          return false;
        }
        return true;
      },
      timeout, "upsert lands", 100ms, /* delay_multiplier= */ 1, 100ms);
}

// Starts one page of the scan over `hash_key`, returning column `v_id`. The callback owns the
// promise, so the read may outlive the caller.
std::future<ReadOutcome> StartReadKeys(
    ybthin_client* client, ybthin_table* table, int hash_key, int32_t v_id, uint64_t limit,
    const std::vector<uint8_t>& paging_state, bool forward_scan = true) {
  ybthin_bind hash_values[] = {I32(hash_key)};
  int32_t target_ids[] = {v_id};
  ybthin_read_op op = {};
  op.table = table;
  op.spec.hash_values = hash_values;
  op.spec.n_hash = 1;
  op.spec.target_ids = target_ids;
  op.spec.n_targets = 1;
  op.spec.limit = limit;
  op.spec.is_forward_scan = forward_scan ? 1 : 0;
  op.paging_state_in = paging_state.empty() ? nullptr : paging_state.data();
  op.paging_state_in_len = paging_state.size();
  auto* promise = new std::promise<ReadOutcome>();
  auto future = promise->get_future();
  ybthin_read_async(
      client, &op, 1, /* read_time_ht= */ 0,
      [](void* ctx, ybthin_status status, ybthin_read_result* result) {
        std::unique_ptr<std::promise<ReadOutcome>> owned(
            static_cast<std::promise<ReadOutcome>*>(ctx));
        OnReadDone(owned.get(), status, result);
      },
      promise);
  return future;
}

// One page of the scan over `hash_key`, returning column `v_id`.
ReadOutcome ReadKeys(
    ybthin_client* client, ybthin_table* table, int hash_key, int32_t v_id, uint64_t limit,
    const std::vector<uint8_t>& paging_state, bool forward_scan = true) {
  return StartReadKeys(client, table, hash_key, v_id, limit, paging_state, forward_scan).get();
}

// The v values of the scan over `hash_key` from `paging_state` on, in scan order. An empty
// `paging_state` scans from the start.
Result<std::vector<int32_t>> ContinueScanKeys(
    ybthin_client* client, ybthin_table* table, int hash_key, int32_t v_id, uint64_t limit,
    std::vector<uint8_t> paging_state, bool forward_scan = true, int max_pages = 100) {
  std::vector<int32_t> values;
  int pages = 0;
  do {
    if (pages++ == max_pages) {
      return STATUS(IllegalState, "paging did not terminate");
    }
    auto out = ReadKeys(client, table, hash_key, v_id, limit, paging_state, forward_scan);
    if (out.code != YBTHIN_OK) {
      return STATUS_FORMAT(
          IllegalState, "read failed with code $0: $1", static_cast<int>(out.code), out.message);
    }
    for (size_t row_idx = 0; row_idx < out.n_rows; ++row_idx) {
      values.push_back(static_cast<int32_t>(out.cells[row_idx * out.n_cols].int_value));
    }
    paging_state = std::move(out.paging_state);
  } while (!paging_state.empty());
  return values;
}

// The v values of the whole scan over `hash_key`, in scan order.
Result<std::vector<int32_t>> ScanKeys(
    ybthin_client* client, ybthin_table* table, int hash_key, int32_t v_id, uint64_t limit,
    bool forward_scan = true, int max_pages = 100) {
  return ContinueScanKeys(
      client, table, hash_key, v_id, limit, /* paging_state= */ {}, forward_scan, max_pages);
}

// Retries the whole scan, as a caller does, until it completes.
Result<std::vector<int32_t>> ScanKeysWithRetries(
    ybthin_client* client, ybthin_table* table, int hash_key, int32_t v_id, uint64_t limit,
    MonoDelta timeout) {
  std::vector<int32_t> values;
  RETURN_NOT_OK(WaitFor(
      [&]() -> Result<bool> {
        auto scanned = ScanKeys(client, table, hash_key, v_id, limit);
        if (!scanned.ok()) {
          LOG(INFO) << "Scan failed: " << scanned.status();
          return false;
        }
        values = std::move(*scanned);
        return true;
      },
      timeout, "scan completes", 100ms, /* delay_multiplier= */ 1, 100ms));
  return values;
}

// Two sessions share one connection, as in a small caller pool.
constexpr ybthin_pool_opts kOneConnectionPool = {
    /* read_sessions= */ 1, /* write_sessions= */ 1, /* sessions_per_conn= */ 4};

}  // namespace

// A timed-out Perform drops its session, and the next call reopens it on the same tserver, so a
// scan pinned to the old session restarts. A Perform sent before the reopen that fails after it
// leaves the reopened session open, so a scan pinned to that one keeps paging.
TEST_F(PgThinClientTest, LateFailureKeepsTheReopenedSession) {
  constexpr int kHashKey = 1;
  constexpr int kRows = 20;
  constexpr uint64_t kPageLimit = 5;
  const uint32_t rpc_timeout_ms = 5000 * kTimeMultiplier;

  const auto oids = ASSERT_RESULT(CreateKeyTable());
  auto client = ASSERT_RESULT(
      CreateThinClient({TServerAddr()}, &kOneConnectionPool, rpc_timeout_ms));
  auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
  const int32_t v_id = table.ColumnId(1);
  {
    auto out = UpsertKeys(client.get(), table.get(), kHashKey, 0, kRows);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }
  auto old_page = ReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {});
  ASSERT_EQ(old_page.code, YBTHIN_OK) << old_page.message;
  ASSERT_FALSE(old_page.paging_state.empty());

  // Both reads are held on the tserver past their RPC timeout, so each times out; the second half a
  // timeout after the first. They are held longer than the test runs, so neither is applied while
  // the cluster shuts down.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_thin_client_perform_delay_ms) = 600000;
  auto dropping_read = StartReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {});
  SleepFor(MonoDelta::FromMilliseconds(rpc_timeout_ms / 2));
  auto late_read = StartReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {});
  {
    auto out = dropping_read.get();
    ASSERT_EQ(out.code, YBTHIN_NETWORK) << out.message;
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_thin_client_perform_delay_ms) = 0;

  auto new_page = ReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {});
  ASSERT_EQ(new_page.code, YBTHIN_OK) << new_page.message;
  ASSERT_FALSE(new_page.paging_state.empty());
  ASSERT_EQ(late_read.wait_for(0s), std::future_status::timeout)
      << "the reopen and its first read took over half an RPC timeout, so the late read had "
      << "already failed";
  {
    auto out = ReadKeys(
        client.get(), table.get(), kHashKey, v_id, kPageLimit, old_page.paging_state);
    ASSERT_EQ(out.code, YBTHIN_READ_RESTART) << out.message;
    ASSERT_STR_CONTAINS(out.message, "reopened");
  }

  {
    auto out = late_read.get();
    ASSERT_EQ(out.code, YBTHIN_NETWORK) << out.message;
  }
  {
    auto out = ReadKeys(
        client.get(), table.get(), kHashKey, v_id, kPageLimit, new_page.paging_state);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }
}

// A fenced batch is an ordinary write failure, so it leaves the connection's other sessions open: a
// scan paged on one of them continues after it.
TEST_F(PgThinClientTest, FencedBatchKeepsOtherSessionsOpen) {
  constexpr int kHashKey = 1;
  constexpr int kRows = 10;
  constexpr uint64_t kPageLimit = 4;
  const auto oids = ASSERT_RESULT(CreateKeyTable());
  auto client = ASSERT_RESULT(CreateThinClient({TServerAddr()}, &kOneConnectionPool, 60000));
  auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
  const int32_t v_id = table.ColumnId(1);

  ASSERT_EQ(UpsertKeys(client.get(), table.get(), kHashKey, 0, kRows).code, YBTHIN_OK);
  auto first_page = ReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {});
  ASSERT_EQ(first_page.code, YBTHIN_OK) << first_page.message;
  ASSERT_FALSE(first_page.paging_state.empty());

  // More than one row, so the tserver combines the per-row errors into one status.
  auto fenced = UpsertKeys(
      client.get(), table.get(), kHashKey, kRows, kRows + 2, /* ignore_after_hybrid_time= */ 1);
  ASSERT_EQ(fenced.code, YBTHIN_FENCED) << fenced.message;

  auto next_page = ReadKeys(
      client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state);
  ASSERT_EQ(next_page.code, YBTHIN_OK) << next_page.message;
}

// Sessions expire after a few seconds. Postgres heartbeats its own sessions often enough to keep
// them, while the thin client's keepalive does not.
class PgThinClientSessionExpiryTest : public PgThinClientTest {
 protected:
  // Not scaled for sanitizers: it must stay below the thin client's 10s keepalive interval.
  static constexpr uint64_t kSessionLifetimeMs = 5000;

  void SetUp() override {
    // Set before the cluster starts, since the session registry schedules its expiry checks by it.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_session_expiration_ms) = kSessionLifetimeMs;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_pg_client_heartbeat_interval_ms) = kSessionLifetimeMs / 3;
    PgThinClientTest::SetUp();
  }
};

// A Perform that finds its session just before the session expires is answered "Session is
// shutting down". That loses the session like an unknown one: the call fails with YBTHIN_NETWORK,
// and its session and the connection's other sessions reopen before their next use.
TEST_F(PgThinClientSessionExpiryTest, ShuttingDownSessionIsLost) {
  constexpr int kHashKey = 1;
  constexpr int kRows = 10;
  constexpr uint64_t kPageLimit = 4;
  const auto oids = ASSERT_RESULT(CreateKeyTable());
  auto client = ASSERT_RESULT(CreateThinClient({TServerAddr()}, &kOneConnectionPool, 60000));
  auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
  const int32_t v_id = table.ColumnId(1);

  ASSERT_EQ(UpsertKeys(client.get(), table.get(), kHashKey, 0, kRows).code, YBTHIN_OK);
  auto first_page = ReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {});
  ASSERT_EQ(first_page.code, YBTHIN_OK) << first_page.message;
  ASSERT_FALSE(first_page.paging_state.empty());

  // Held past two lifetimes, since a keepalive ping may postpone the expiry once.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_thin_client_perform_delay_ms) =
      2 * kSessionLifetimeMs + 1000 * kTimeMultiplier;
  auto lost = UpsertKeys(client.get(), table.get(), kHashKey, kRows, 2 * kRows);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_thin_client_perform_delay_ms) = 0;
  ASSERT_EQ(lost.code, YBTHIN_NETWORK) << lost.message;
  ASSERT_STR_CONTAINS(lost.message, "Session is shutting down");

  // The read session shares the connection, so the scan pinned to it restarts.
  auto next_page = ReadKeys(
      client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state);
  ASSERT_EQ(next_page.code, YBTHIN_READ_RESTART) << next_page.message;
  // The write session reopens, so the retried upsert lands.
  auto retried = UpsertKeys(client.get(), table.get(), kHashKey, kRows, 2 * kRows);
  ASSERT_EQ(retried.code, YBTHIN_OK) << retried.message;
}

// Three tservers, so one can stop while its tablets stay served. Postgres runs on tserver 0, so the
// thin client talks to the other two.
class PgThinClientFailoverTest : public PgThinClientTest {
 protected:
  size_t NumTabletServers() override { return 3; }

  std::string TServerAddrOf(size_t ts_idx) const {
    return cluster_->mini_tablet_server(ts_idx)->bound_rpc_addr_str();
  }

  uint64_t PerformCount(size_t ts_idx) {
    return cluster_->mini_tablet_server(ts_idx)
        ->metric_entity()
        .FindOrCreateMetric<Histogram>(
            &METRIC_handler_latency_yb_tserver_ThinClientService_Perform)
        ->underlying()
        ->TotalCount();
  }
};

// A pool on one connection talks to one tserver. When that tserver stops, the connection moves to
// the other one: a retried write lands there, and a scan pinned to the old session restarts.
// Opening a table skips the stopped tserver too.
TEST_F(PgThinClientFailoverTest, OneConnectionPoolMovesOffAStoppedTserver) {
  constexpr size_t kStoppedTs = 1;
  constexpr size_t kLiveTs = 2;
  constexpr int kHashKey = 1;
  constexpr int kRowsBefore = 20;
  constexpr int kRowsAfter = 20;
  constexpr uint64_t kPageLimit = 5;

  const auto oids = ASSERT_RESULT(CreateKeyTable());
  // Connection 0 starts on the first address.
  const std::vector<std::string> addrs = {TServerAddrOf(kStoppedTs), TServerAddrOf(kLiveTs)};
  auto client = ASSERT_RESULT(CreateThinClient(addrs, &kOneConnectionPool, 60000));
  auto reopening_client = ASSERT_RESULT(CreateThinClient(addrs, &kOneConnectionPool, 60000));
  auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
  const int32_t v_id = table.ColumnId(1);

  {
    auto out = UpsertKeys(client.get(), table.get(), kHashKey, 0, kRowsBefore);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }
  auto first_page = ReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {});
  ASSERT_EQ(first_page.code, YBTHIN_OK) << first_page.message;
  ASSERT_FALSE(first_page.paging_state.empty());
  ASSERT_GT(PerformCount(kStoppedTs), 0U);
  ASSERT_EQ(PerformCount(kLiveTs), 0U) << "the whole pool should ride connection 0";

  cluster_->mini_tablet_server(kStoppedTs)->Shutdown();

  ASSERT_OK(UpsertKeysWithRetries(
      client.get(), table.get(), kHashKey, kRowsBefore, kRowsBefore + kRowsAfter,
      30s * kTimeMultiplier));
  ASSERT_GT(PerformCount(kLiveTs), 0U);

  auto next_page = ReadKeys(
      client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state);
  ASSERT_EQ(next_page.code, YBTHIN_READ_RESTART) << next_page.message;
  ASSERT_EQ(
      ASSERT_RESULT(ScanKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit)).size(),
      static_cast<size_t>(kRowsBefore + kRowsAfter));

  // As a recycle does after a tserver goes away.
  auto reopened_table = ASSERT_RESULT(ThinTable::Open(reopening_client.get(), oids));
}

// Creating a client, as a recycle does, succeeds while a configured tserver is unreachable.
TEST_F(PgThinClientFailoverTest, CreateSkipsAnUnreachableTserver) {
  constexpr int kHashKey = 1;
  constexpr int kRows = 20;

  const auto oids = ASSERT_RESULT(CreateKeyTable());
  // Nothing listens on this port, and connection 0 starts on it.
  std::unique_ptr<FileLock> port_lock;
  const auto unreachable_addr = HostPort("127.0.0.1", GetFreePort(&port_lock)).ToString();
  auto client = ASSERT_RESULT(CreateThinClient(
      {unreachable_addr, TServerAddrOf(1)}, &kOneConnectionPool, 60000));
  auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
  {
    auto out = UpsertKeys(client.get(), table.get(), kHashKey, 0, kRows);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }
  ASSERT_EQ(
      ASSERT_RESULT(ScanKeys(
          client.get(), table.get(), kHashKey, table.ColumnId(1), /* limit= */ 0)).size(),
      static_cast<size_t>(kRows));
}

// Only the connections on a stopped tserver move, so a scan pinned to a session on another
// connection keeps paging without a restart.
TEST_F(PgThinClientFailoverTest, ScansOnOtherConnectionsKeepPaging) {
  constexpr size_t kStoppedTs = 1;
  constexpr size_t kLiveTs = 2;
  constexpr int kHashKey = 1;
  constexpr int kRows = 20;
  constexpr uint64_t kPageLimit = 5;
  // One session per connection, and connection i starts on addrs[i % 2], so read session i is on
  // the stopped tserver for even i.
  constexpr uint32_t kReadSessions = 4;
  constexpr ybthin_pool_opts kPool = {
      kReadSessions, /* write_sessions= */ 1, /* sessions_per_conn= */ 1};

  const auto oids = ASSERT_RESULT(CreateKeyTable());
  // One reactor per connection is plenty for a test that sends one call at a time.
  auto client = ASSERT_RESULT(CreateThinClient(
      {TServerAddrOf(kStoppedTs), TServerAddrOf(kLiveTs)}, &kPool, 60000,
      /* num_reactors= */ 1));
  auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
  const int32_t v_id = table.ColumnId(1);
  {
    auto out = UpsertKeys(client.get(), table.get(), kHashKey, 0, kRows);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }

  // Fresh scans round-robin the read sessions, so scan i is pinned to read session i.
  std::vector<ReadOutcome> first_pages;
  for (uint32_t scan_idx = 0; scan_idx < kReadSessions; ++scan_idx) {
    first_pages.push_back(ReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {}));
    ASSERT_EQ(first_pages.back().code, YBTHIN_OK) << first_pages.back().message;
    ASSERT_FALSE(first_pages.back().paging_state.empty());
  }
  ASSERT_GT(PerformCount(kLiveTs), 0U);

  cluster_->mini_tablet_server(kStoppedTs)->Shutdown();

  for (uint32_t scan_idx = 0; scan_idx < kReadSessions; ++scan_idx) {
    const auto& first_page = first_pages[scan_idx];
    if (scan_idx % 2 == 0) {
      // The first call to reach the stopped tserver gets NETWORK and drops its session, so the
      // retry restarts. If the keepalive got there first, the first try restarts.
      auto out = ReadKeys(
          client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state);
      if (out.code == YBTHIN_NETWORK) {
        out = ReadKeys(
            client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state);
      }
      ASSERT_EQ(out.code, YBTHIN_READ_RESTART) << "scan " << scan_idx << ": " << out.message;
      continue;
    }
    const auto rest = ASSERT_RESULT(ContinueScanKeys(
        client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state));
    ASSERT_EQ(first_page.n_rows + rest.size(), static_cast<size_t>(kRows)) << "scan " << scan_idx;
  }
}

namespace {

// Forwards each accepted connection to the next backend in turn, as a Kubernetes ClusterIP Service
// picks a pod per connection.
class RoundRobinBalancer {
 public:
  explicit RoundRobinBalancer(std::vector<Endpoint> backends)
      : backends_(std::move(backends)), acceptor_(io_context_) {}

  ~RoundRobinBalancer() {
    io_context_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  Status Start(const Endpoint& local) {
    boost::system::error_code ec;
    acceptor_.open(local.protocol(), ec);
    if (!ec) {
      acceptor_.bind(local, ec);
    }
    if (!ec) {
      acceptor_.listen(boost::asio::socket_base::max_listen_connections, ec);
    }
    if (ec) {
      return STATUS_FORMAT(NetworkError, "Balancer cannot listen on $0: $1", local, ec.message());
    }
    Accept();
    thread_ = std::thread([this] { io_context_.run(); });
    return Status::OK();
  }

  // Connections accepted so far.
  size_t Accepted() const { return next_backend_.load(std::memory_order_acquire); }

  // Closes every forwarded connection, as when a backend restarts or the Service resets it.
  void DropConnections() {
    std::promise<void> done;
    boost::asio::post(io_context_, [this, &done] {
      for (auto& pipe : pipes_) {
        if (auto live = pipe.lock()) {
          live->Close();
        }
      }
      pipes_.clear();
      done.set_value();
    });
    done.get_future().wait();
  }

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Buffer = std::array<char, 4096>;

  // Both directions of one forwarded connection; either side closing ends both.
  struct Pipe : std::enable_shared_from_this<Pipe> {
    Pipe(Socket&& client_socket, boost::asio::io_context& io_context)
        : client(std::move(client_socket)), backend(io_context) {}

    void Start(const Endpoint& to) {
      backend.async_connect(to, [self = shared_from_this()](const boost::system::error_code& ec) {
        if (ec) {
          self->Close();
          return;
        }
        self->Relay(self->client, self->backend, self->to_backend);
        self->Relay(self->backend, self->client, self->to_client);
      });
    }

    void Relay(Socket& from, Socket& to, Buffer& buffer) {
      from.async_read_some(
          boost::asio::buffer(buffer),
          [self = shared_from_this(), &from, &to, &buffer](
              const boost::system::error_code& ec, size_t size) {
            if (ec) {
              self->Close();
              return;
            }
            boost::asio::async_write(
                to, boost::asio::buffer(buffer.data(), size),
                [self, &from, &to, &buffer](const boost::system::error_code& ec, size_t) {
                  if (ec) {
                    self->Close();
                    return;
                  }
                  self->Relay(from, to, buffer);
                });
          });
    }

    void Close() {
      boost::system::error_code ec;
      client.close(ec);
      backend.close(ec);
    }

    Socket client;
    Socket backend;
    Buffer to_backend;
    Buffer to_client;
  };

  void Accept() {
    acceptor_.async_accept([this](const boost::system::error_code& ec, Socket client) {
      if (ec) {
        return;
      }
      auto pipe = std::make_shared<Pipe>(std::move(client), io_context_);
      pipes_.push_back(pipe);
      pipe->Start(backends_[next_backend_++ % backends_.size()]);
      Accept();
    });
  }

  boost::asio::io_context io_context_;
  const std::vector<Endpoint> backends_;
  boost::asio::ip::tcp::acceptor acceptor_;
  std::atomic<size_t> next_backend_{0};
  std::vector<std::weak_ptr<Pipe>> pipes_;
  std::thread thread_;
};

// Starts `balancer` on a free local port, held by `port_lock`, and returns its address.
Result<std::string> StartBalancer(
    RoundRobinBalancer& balancer, std::unique_ptr<FileLock>* port_lock) {
  const auto addr = HostPort("127.0.0.1", GetFreePort(port_lock)).ToString();
  RETURN_NOT_OK(balancer.Start(VERIFY_RESULT(ParseEndpoint(addr, 0))));
  return addr;
}

}  // namespace

// A balancer over the first and second tservers, in front of a one-connection client.
class PgThinClientLoadBalancerTest : public PgThinClientFailoverTest {
 protected:
  static constexpr size_t kFirstTs = 1;
  static constexpr size_t kSecondTs = 2;
  static constexpr int kHashKey = 1;
  static constexpr uint64_t kPageLimit = 4;

  void DoTearDown() override {
    table_.reset();
    client_.reset();
    other_client_.reset();
    balancer_.reset();
    PgThinClientFailoverTest::DoTearDown();
  }

  void StartClientBehindBalancer() {
    const auto oids = ASSERT_RESULT(CreateKeyTable());

    // Other clients' sessions on the tserver the balancer picks first put its session ids ahead of
    // the other's, as on a shared universe. A Perform on the wrong tserver then finds no session,
    // rather than another client's that happens to share its id.
    constexpr ybthin_pool_opts kOtherClientsPool = {
        /* read_sessions= */ 8, /* write_sessions= */ 1, /* sessions_per_conn= */ 0};
    other_client_ = ASSERT_RESULT(
        CreateThinClient({TServerAddrOf(kFirstTs)}, &kOtherClientsPool, 60000));

    balancer_ = std::make_unique<RoundRobinBalancer>(std::vector<Endpoint>{
        cluster_->mini_tablet_server(kFirstTs)->bound_rpc_addr(),
        cluster_->mini_tablet_server(kSecondTs)->bound_rpc_addr()});
    const auto balancer_addr = ASSERT_RESULT(StartBalancer(*balancer_, &port_lock_));

    client_ = ASSERT_RESULT(CreateThinClient({balancer_addr}, &kOneConnectionPool, 60000));
    table_.emplace(ASSERT_RESULT(ThinTable::Open(client_.get(), oids)));
    v_id_ = table_->ColumnId(1);
  }

  std::unique_ptr<FileLock> port_lock_;
  std::unique_ptr<RoundRobinBalancer> balancer_;
  ThinClientPtr other_client_;
  ThinClientPtr client_;
  std::optional<ThinTable> table_;
  int32_t v_id_ = 0;
};

// A load balancer, like a Kubernetes ClusterIP Service, picks a tserver per TCP connection, while a
// session id is known only to the tserver it opened on. So a connection's Performs must ride the
// one socket its sessions opened on.
TEST_F(PgThinClientLoadBalancerTest, ConnectionStaysOnOneTserverBehindALoadBalancer) {
  constexpr int kRounds = 5;
  constexpr int kRowsPerRound = 10;
  ASSERT_NO_FATALS(StartClientBehindBalancer());

  for (int round = 0; round < kRounds; ++round) {
    auto out = UpsertKeys(
        client_.get(), table_->get(), kHashKey, round * kRowsPerRound,
        (round + 1) * kRowsPerRound);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
    ASSERT_EQ(
        ASSERT_RESULT(ScanKeys(client_.get(), table_->get(), kHashKey, v_id_, kPageLimit)).size(),
        static_cast<size_t>((round + 1) * kRowsPerRound));
  }
  ASSERT_GT(PerformCount(kFirstTs), 0U);
  ASSERT_EQ(PerformCount(kSecondTs), 0U) << "the pool's one connection should stay on one tserver";
}

// When the socket drops, the reconnect can reach the other tserver, where none of the connection's
// sessions exist. The first call to fail drops them all, so the others reopen without failing too.
TEST_F(PgThinClientLoadBalancerTest, SessionsReopenTogetherAfterTheSocketDrops) {
  constexpr int kRows = 10;
  ASSERT_NO_FATALS(StartClientBehindBalancer());
  ASSERT_EQ(UpsertKeys(client_.get(), table_->get(), kHashKey, 0, kRows).code, YBTHIN_OK);
  ASSERT_EQ(
      ASSERT_RESULT(ScanKeys(client_.get(), table_->get(), kHashKey, v_id_, kPageLimit)).size(),
      static_cast<size_t>(kRows));
  ASSERT_EQ(PerformCount(kSecondTs), 0U);
  const auto first_ts_performs = PerformCount(kFirstTs);

  balancer_->DropConnections();

  // The write session may be the first to find out.
  auto out = UpsertKeys(client_.get(), table_->get(), kHashKey, kRows, 2 * kRows);
  if (out.code != YBTHIN_OK) {
    ASSERT_EQ(out.code, YBTHIN_NETWORK) << out.message;
    out = UpsertKeys(client_.get(), table_->get(), kHashKey, kRows, 2 * kRows);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }
  // The read session shares the connection, so it reopened with the write session.
  ASSERT_EQ(
      ASSERT_RESULT(ScanKeys(client_.get(), table_->get(), kHashKey, v_id_, kPageLimit)).size(),
      static_cast<size_t>(2 * kRows));
  // So both sessions now live on the second tserver.
  ASSERT_EQ(PerformCount(kFirstTs), first_ts_performs);
  ASSERT_GT(PerformCount(kSecondTs), 0U);
}

// A connection keeps one socket to its host however many addresses it is given, since any of them
// may be a load balancer.
TEST_F(PgThinClientTest, OneSocketPerConnection) {
  constexpr int kUpserts = 20;
  const auto oids = ASSERT_RESULT(CreateKeyTable());
  const std::vector<Endpoint> tserver = {cluster_->mini_tablet_server(0)->bound_rpc_addr()};
  for (size_t num_addrs : {1, 2}) {
    SCOPED_TRACE(Format("$0 addresses", num_addrs));
    // Forwarders to the one tserver stand in for the addresses, and count the sockets opened.
    std::vector<std::unique_ptr<FileLock>> port_locks(num_addrs);
    std::vector<std::unique_ptr<RoundRobinBalancer>> forwarders;
    std::vector<std::string> addrs;
    for (auto& port_lock : port_locks) {
      forwarders.push_back(std::make_unique<RoundRobinBalancer>(tserver));
      addrs.push_back(ASSERT_RESULT(StartBalancer(*forwarders.back(), &port_lock)));
    }
    auto client = ASSERT_RESULT(CreateThinClient(addrs, &kOneConnectionPool, 60000));
    auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
    for (int v = 0; v < kUpserts; ++v) {
      ASSERT_EQ(UpsertKeys(client.get(), table.get(), /* hash_key= */ 1, v, v + 1).code, YBTHIN_OK);
    }
    // The pool's one connection talks to the first address.
    ASSERT_EQ(forwarders.front()->Accepted(), 1U);
  }
}

// The cluster runs with node-to-node encryption; the thin client connects over TLS, authenticating
// the server against the test CA.
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

// client_create opens a session via a Heartbeat RPC, so success proves the TLS handshake plus an
// encrypted RPC round-trip. This fixture cannot open a SQL connection -- it forks postgres in
// process without propagating certs_dir, so pggate's secure context fails to initialize there --
// so the data path over TLS is covered by PgThinClientExternalTlsTest below instead.
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

// Runs the shim against an ExternalMiniCluster, with SQL through psql.
class PgThinClientExternalTest : public PgCommandTestBase {
 protected:
  explicit PgThinClientExternalTest(bool encrypted)
      : PgCommandTestBase(/* auth= */ false, encrypted) {}

  Result<uint32_t> FetchOidViaPsql(const std::string& query) {
    auto out = VERIFY_RESULT(RunPsqlCommand(query, TuplesOnly::kTrue));
    return static_cast<uint32_t>(std::stoul(out));
  }

  Result<TableOids> CreateKeyTable() {
    RETURN_NOT_OK(RunPsqlCommand(kKeyTableDdl, TuplesOnly::kFalse, CheckErrorString::kTrue));
    const auto db_oid = VERIFY_RESULT(FetchOidViaPsql(kDbOidQuery));
    return TableOids{db_oid, VERIFY_RESULT(FetchOidViaPsql(kKeyTableOidQuery))};
  }
};

// The tservers here are separate processes started with --certs_dir, so the postgres they fork can
// initialize pggate's secure context and serve SQL -- which is what the in-process fixture above
// cannot do. That makes it possible to drive the shim's data path over TLS: SQL sets the table up
// and cross-checks the result, while every shim op rides the encrypted stream.
class PgThinClientExternalTlsTest : public PgThinClientExternalTest {
 protected:
  PgThinClientExternalTlsTest() : PgThinClientExternalTest(/* encrypted= */ true) {}

  // One tserver keeps the external cluster cheap; the shim only talks to one endpoint here. RF has
  // to come down with it, or creating the transaction status table fails and global initdb dies.
  int GetNumTabletServers() const override { return 1; }

  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    PgCommandTestBase::UpdateMiniClusterOptions(options);
    options->replication_factor = 1;
  }

  std::string TServerRpcAddr() const { return pg_ts->bound_rpc_addr().ToString(); }

  std::string CaCertPath() const { return JoinPathSegments(GetCertsDir(), "ca.crt"); }
};

TEST_F(PgThinClientExternalTlsTest, UpsertAndReadOverTls) {
  constexpr int kHashKey = 1;
  constexpr int kNumRows = 50;

  CreateTable("CREATE TABLE t (k int, v int, PRIMARY KEY((k) HASH, v))");
  const auto db_oid = ASSERT_RESULT(FetchOidViaPsql(
      "SELECT oid FROM pg_database WHERE datname = current_database()"));
  const auto table_oid = ASSERT_RESULT(FetchOidViaPsql("SELECT 't'::regclass::oid"));

  const auto addr = TServerRpcAddr();
  const char* addrs[] = {addr.c_str()};
  const auto ca = CaCertPath();
  ybthin_tls_opts tls = {};
  tls.ca_cert_path = ca.c_str();

  ybthin_client* client = nullptr;
  {
    auto st = ybthin_client_create(
        addrs, 1, &tls, /* pool= */ nullptr, /* rpc_timeout_ms= */ 60000, /* num_reactors= */ 0,
        &client);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }
  ybthin_table* table = nullptr;
  ybthin_table_info info = {};
  {
    auto st = ybthin_table_open(client, db_oid, table_oid, &table, &info);
    ASSERT_EQ(st.code, YBTHIN_OK) << (st.message ? st.message : "");
  }
  ASSERT_EQ(info.n_columns, 2);
  const int32_t v_id = info.columns[1].id;

  // Upsert over TLS.
  std::vector<std::array<ybthin_bind, 2>> keys(kNumRows);
  std::vector<ybthin_upsert_row> rows(kNumRows);
  for (int row_idx = 0; row_idx < kNumRows; ++row_idx) {
    keys[row_idx] = {I32(kHashKey), I32(row_idx)};
    rows[row_idx] = ybthin_upsert_row{table, keys[row_idx].data(), 2, nullptr, nullptr, 0, 0};
  }
  {
    std::promise<WriteOutcome> promise;
    auto future = promise.get_future();
    ybthin_upsert_batch_async(client, rows.data(), rows.size(), &OnWriteDone, &promise);
    auto out = future.get();
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }

  // Cross-check the encrypted writes through SQL.
  RunPsqlCommand(Format("SELECT count(*) FROM t WHERE k = $0", kHashKey),
                 Format("count\n-------\n    $0\n(1 row)", kNumRows));

  // Read them back over TLS.
  auto seen = ASSERT_RESULT(ScanKeys(
      client, table, kHashKey, v_id, /* limit= */ 0, /* forward_scan= */ false,
      /* max_pages= */ 20));
  std::sort(seen.begin(), seen.end());
  ASSERT_EQ(seen.size(), static_cast<size_t>(kNumRows));
  for (int row_idx = 0; row_idx < kNumRows; ++row_idx) {
    ASSERT_EQ(seen[row_idx], row_idx);
  }

  ybthin_columns_free(info.columns, info.n_columns);
  ybthin_table_close(table);
  ybthin_client_destroy(client);
}

// Three tservers in separate processes, so one can be frozen. Postgres runs on tserver 0, so the
// thin client talks to the other two.
class PgThinClientExternalFailoverTest : public PgThinClientExternalTest {
 protected:
  PgThinClientExternalFailoverTest() : PgThinClientExternalTest(/* encrypted= */ false) {}
};

// A frozen tserver, like a partitioned pod, accepts connections but never answers, so every RPC to
// it times out instead of failing fast. The heartbeat that reopens the session times out too, and
// that moves the connection to the other tserver.
TEST_F(PgThinClientExternalFailoverTest, WritesMoveOffAFrozenTserver) {
  constexpr int kHashKey = 1;
  constexpr int kRowsBefore = 20;
  constexpr int kRowsAfter = 20;
  // Short, so each timeout the failover waits out is short too.
  constexpr uint32_t kRpcTimeoutMs = 5000;

  const auto oids = ASSERT_RESULT(CreateKeyTable());
  // Connection 0 starts on the first address.
  auto* frozen_ts = cluster_->tablet_server(1);
  auto client = ASSERT_RESULT(CreateThinClient(
      {frozen_ts->bound_rpc_addr().ToString(),
       cluster_->tablet_server(2)->bound_rpc_addr().ToString()},
      &kOneConnectionPool, kRpcTimeoutMs));
  auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
  {
    auto out = UpsertKeys(client.get(), table.get(), kHashKey, 0, kRowsBefore);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }

  ASSERT_OK(frozen_ts->Pause());
  ScopedResumeExternalDaemon resume_frozen_ts(frozen_ts);
  const auto frozen_at = CoarseMonoClock::Now();

  ASSERT_OK(UpsertKeysWithRetries(
      client.get(), table.get(), kHashKey, kRowsBefore, kRowsBefore + kRowsAfter,
      60s * kTimeMultiplier));
  LOG(INFO) << "Writes landed " << MonoDelta(CoarseMonoClock::Now() - frozen_at)
            << " after the tserver froze";

  RunPsqlCommand(Format("SELECT count(*) FROM t WHERE k = $0", kHashKey),
                 Format("count\n-------\n    $0\n(1 row)", kRowsBefore + kRowsAfter));
  ASSERT_EQ(
      ASSERT_RESULT(ScanKeys(
          client.get(), table.get(), kHashKey, table.ColumnId(1), /* limit= */ 0)).size(),
      static_cast<size_t>(kRowsBefore + kRowsAfter));
}

// A page sent to a frozen tserver times out, which drops its session, so retrying the page reports
// a restart. A fresh scan then moves to the other tserver.
TEST_F(PgThinClientExternalFailoverTest, PagedScanAcrossAFrozenTserver) {
  constexpr int kHashKey = 1;
  constexpr int kRows = 20;
  constexpr uint64_t kPageLimit = 5;
  constexpr uint32_t kRpcTimeoutMs = 5000;

  const auto oids = ASSERT_RESULT(CreateKeyTable());
  // Connection 0 starts on the first address.
  auto* frozen_ts = cluster_->tablet_server(1);
  auto client = ASSERT_RESULT(CreateThinClient(
      {frozen_ts->bound_rpc_addr().ToString(),
       cluster_->tablet_server(2)->bound_rpc_addr().ToString()},
      &kOneConnectionPool, kRpcTimeoutMs));
  auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
  const int32_t v_id = table.ColumnId(1);
  {
    auto out = UpsertKeys(client.get(), table.get(), kHashKey, 0, kRows);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }
  auto first_page = ReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {});
  ASSERT_EQ(first_page.code, YBTHIN_OK) << first_page.message;
  ASSERT_FALSE(first_page.paging_state.empty());

  ASSERT_OK(frozen_ts->Pause());
  ScopedResumeExternalDaemon resume_frozen_ts(frozen_ts);

  {
    auto out = ReadKeys(
        client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state);
    ASSERT_EQ(out.code, YBTHIN_NETWORK) << out.message;
  }
  {
    auto out = ReadKeys(
        client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state);
    ASSERT_EQ(out.code, YBTHIN_READ_RESTART) << out.message;
    ASSERT_STR_CONTAINS(out.message, "no longer open");
  }
  ASSERT_EQ(
      ASSERT_RESULT(ScanKeysWithRetries(
          client.get(), table.get(), kHashKey, v_id, kPageLimit, 60s * kTimeMultiplier)).size(),
      static_cast<size_t>(kRows));
}

// A page in flight when its tserver dies fails, and retrying it reports a restart. The broken
// connection moved, so a fresh scan runs on the other tserver.
TEST_F(PgThinClientExternalFailoverTest, PageInFlightWhenItsTserverDies) {
  constexpr int kHashKey = 1;
  constexpr int kRows = 20;
  constexpr uint64_t kPageLimit = 5;
  // Long, so the page fails because its tserver died, not because it timed out.
  constexpr uint32_t kRpcTimeoutMs = 60000;

  const auto oids = ASSERT_RESULT(CreateKeyTable());
  // Connection 0 starts on the first address.
  auto* dying_ts = cluster_->tablet_server(1);
  auto client = ASSERT_RESULT(CreateThinClient(
      {dying_ts->bound_rpc_addr().ToString(),
       cluster_->tablet_server(2)->bound_rpc_addr().ToString()},
      &kOneConnectionPool, kRpcTimeoutMs));
  auto table = ASSERT_RESULT(ThinTable::Open(client.get(), oids));
  const int32_t v_id = table.ColumnId(1);
  {
    auto out = UpsertKeys(client.get(), table.get(), kHashKey, 0, kRows);
    ASSERT_EQ(out.code, YBTHIN_OK) << out.message;
  }
  auto first_page = ReadKeys(client.get(), table.get(), kHashKey, v_id, kPageLimit, {});
  ASSERT_EQ(first_page.code, YBTHIN_OK) << first_page.message;
  ASSERT_FALSE(first_page.paging_state.empty());

  // Frozen, the tserver holds the page unanswered until it is killed. The guard resumes it if the
  // test fails first, so teardown can stop it.
  ASSERT_OK(dying_ts->Pause());
  ScopedResumeExternalDaemon resume_dying_ts(dying_ts);
  auto in_flight = StartReadKeys(
      client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state);
  ASSERT_EQ(in_flight.wait_for(1s), std::future_status::timeout);
  dying_ts->Shutdown();
  {
    auto out = in_flight.get();
    ASSERT_EQ(out.code, YBTHIN_NETWORK) << out.message;
  }
  {
    auto out = ReadKeys(
        client.get(), table.get(), kHashKey, v_id, kPageLimit, first_page.paging_state);
    ASSERT_EQ(out.code, YBTHIN_READ_RESTART) << out.message;
  }
  ASSERT_EQ(
      ASSERT_RESULT(ScanKeysWithRetries(
          client.get(), table.get(), kHashKey, v_id, kPageLimit, 120s * kTimeMultiplier)).size(),
      static_cast<size_t>(kRows));
}

}  // namespace yb::pgwrapper
