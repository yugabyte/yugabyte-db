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
// yb_thin_client.cc -- a thin, Perform-based YugabyteDB tserver client.
//
// Implements the C ABI in yb_thin_client.h by speaking yb.tserver.ThinClientService.Perform
// directly to a tserver RPC endpoint via ThinClientServiceProxy. There is deliberately NO YBClient
// / YBSession / Batcher / metacache / TabletInvoker: each op carries its own routing info
// (hash_code + partition_key) and the tserver routes it, exactly as it does for pggate.
//
// This library is upgraded BEFORE the tserver, so it must keep working against an OLDER one -- see
// yb_thin_client.h for the backward-compatibility rule.
//

#include "yb/thin_client/yb_thin_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "yb/common/common.pb.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/value.pb.h"
#include "yb/common/wire_protocol.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_entry_value.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/endian.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"

#include "yb/tserver/thin_client.pb.h"
#include "yb/tserver/thin_client.proxy.h"

#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pggate/util/pg_wire.h"

#include "yb/util/env.h"
#include "yb/util/faststring.h"
#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

using yb::DataType;
using yb::faststring;
using yb::HostPort;
using yb::MonoDelta;
using yb::RefCntSlice;
using yb::Result;
using yb::Schema;
using yb::Slice;
using yb::Status;
using yb::dockv::PartitionSchema;

namespace tserver = yb::tserver;
namespace rpc = yb::rpc;
namespace pggate = yb::pggate;
namespace dockv = yb::dockv;

static constexpr uint16_t kDefaultTserverRpcPort = 9100;
// Well inside the server's default session expiry window.
static constexpr int kKeepaliveIntervalMs = 10000;
static constexpr int kDefaultRpcTimeoutMs = 60000;
static constexpr int kDefaultNumReactors = 4;
// Concurrency comes from multiple sessions, packed 4 per connection so a small pool opens one.
static constexpr uint32_t kDefaultReadSessions = 4;
static constexpr uint32_t kDefaultWriteSessions = 1;
static constexpr uint32_t kDefaultSessionsPerConn = 4;
// One connection to a tserver. Its own messenger, hence its own socket, so N connections spread
// across tserver nodes behind a ClusterIP VIP. Sessions are packed onto connections.
struct ybthin_connection {
  std::unique_ptr<rpc::Messenger> messenger;
  std::unique_ptr<rpc::ProxyCache> proxy_cache;
  std::unique_ptr<tserver::ThinClientServiceProxy> proxy;
  HostPort host;
};

// One ThinClientService session. The server keeps no per-session state, so Performs need no
// ordering and a session's batches may be in flight concurrently. Reopening bumps `generation`,
// invalidating scans pinned to the old incarnation (YBTHIN_READ_RESTART).
struct ybthin_session {
  size_t conn_index = 0;
  std::mutex mutex;
  bool open GUARDED_BY(mutex) = false;
  uint32_t generation GUARDED_BY(mutex) = 0;
  uint64_t session_id GUARDED_BY(mutex) = 0;
  uint64_t stmt_id GUARDED_BY(mutex) = 1;
};

struct ybthin_client {
  std::unique_ptr<rpc::SecureContext> secure_context;  // null for plaintext; shared by all conns
  std::vector<std::unique_ptr<ybthin_connection>> connections;
  std::vector<std::unique_ptr<ybthin_session>> read_sessions;
  std::vector<std::unique_ptr<ybthin_session>> write_sessions;
  // Round-robin cursors for picking the next session out of each pool.
  std::atomic<size_t> read_session_cursor{0};
  std::atomic<size_t> write_session_cursor{0};
  std::vector<HostPort> hosts;
  MonoDelta timeout;
  int num_reactors = kDefaultNumReactors;

  // std::thread, not yb::Thread: a self-contained .so must not pull in YB's global thread registry.
  std::thread keepalive_thread;  // NOLINT(build/std_thread)
  std::mutex mutex;
  std::condition_variable cv;
  bool stop = false;
};

struct ybthin_table {
  struct ColInfo {
    std::string name;
    int32_t id;
    ybthin_col_kind kind;
    ybthin_value_type type;
  };
  std::string table_id;
  uint32_t schema_version = 0;
  Schema schema;
  PartitionSchema partition_schema;
  std::vector<ColInfo> columns;  // owns the name strings referenced by ybthin_column
};

namespace {

// Every buffer the shim hands back to the caller is malloc/free.
char* DupCString(const std::string& str) {
  char* buf = static_cast<char*>(malloc(str.size() + 1));
  if (buf) {
    memcpy(buf, str.data(), str.size());
    buf[str.size()] = '\0';
  }
  return buf;
}

uint8_t* DupBytes(const void* data, size_t len) {
  if (len == 0) {
    return nullptr;
  }
  uint8_t* buf = static_cast<uint8_t*>(malloc(len));
  if (buf) {
    memcpy(buf, data, len);
  }
  return buf;
}

ybthin_status OkStatus() { return ybthin_status{YBTHIN_OK, nullptr}; }

ybthin_status MakeStatus(ybthin_status_code code, const std::string& msg) {
  return ybthin_status{code, code == YBTHIN_OK ? nullptr : DupCString(msg)};
}

ybthin_status_code ClassifyStatus(const Status& status) {
  if (status.ok()) {
    return YBTHIN_OK;
  }
  if (status.IsTimedOut() || status.IsNetworkError() || status.IsRemoteError()) {
    return YBTHIN_NETWORK;
  }
  if (status.IsTryAgain() || status.IsServiceUnavailable() || status.IsBusy()) {
    return YBTHIN_TRY_AGAIN;
  }
  if (status.IsInvalidArgument() || status.IsNotSupported()) {
    return YBTHIN_INVALID;
  }
  // The only Expired status this service produces is a fenced write (RaftConsensus rejects the op
  // when ignore_after_hybrid_time has passed), so the caller can tell it apart from a failure that
  // might still have taken effect. A fenced write definitively did not.
  if (status.IsExpired()) {
    return YBTHIN_FENCED;
  }
  return YBTHIN_OTHER;
}

// True for the retryable-request layer's "I already applied this" verdict on a WRITE.
//
// DocDB dedupes writes on (client_id, request_id) and rejects a replay of an id it has already
// REPLICATED (consensus/retryable_requests.cc). An id only reaches that set by going through Raft,
// so the verdict means the write is durable -- for an idempotent upsert, which is every write this
// ABI can express, the caller's success condition is already met. Reporting it as an error wedges a
// caller that replays on failure: the replay is byte-identical, so it is rejected identically.
//
// Deliberately NOT a plain IsAlreadyPresent() test. pggate overloads that category to carry a
// unique violation (pg_perform_future.cc PatchStatus, pg_operation_buffer.cc), so match only a
// status with no PGSQL error code attached -- a PG-level error always carries one.
bool IsAlreadyReplicatedWrite(const Status& status) {
  return status.IsAlreadyPresent() && !yb::PgsqlError::ValueFromStatus(status);
}

ybthin_status FromStatus(const Status& status) {
  if (status.ok()) {
    return OkStatus();
  }
  auto code = ClassifyStatus(status);
  const auto msg = status.ToString();
  // A dropped/expired session surfaces as an app error; steer the caller to reconnect.
  if (code == YBTHIN_OTHER || code == YBTHIN_INVALID) {
    if (msg.find("ession") != std::string::npos &&
        (msg.find("nknown") != std::string::npos || msg.find("xpired") != std::string::npos)) {
      code = YBTHIN_NETWORK;
    }
  }
  return MakeStatus(code, msg);
}

Status BindToQLValue(const ybthin_bind& bind, yb::QLValuePB* out) {
  switch (bind.tag) {
    case YBTHIN_BIND_NULL:
      return Status::OK();  // absent oneof == SQL NULL
    case YBTHIN_BIND_BOOL:
      out->set_bool_value(bind.int_value != 0);
      return Status::OK();
    case YBTHIN_BIND_I16:
      out->set_int16_value(static_cast<int32_t>(bind.int_value));
      return Status::OK();
    case YBTHIN_BIND_I32:
      out->set_int32_value(static_cast<int32_t>(bind.int_value));
      return Status::OK();
    case YBTHIN_BIND_I64:
      out->set_int64_value(bind.int_value);
      return Status::OK();
    case YBTHIN_BIND_TEXT:
      out->set_string_value(bind.bytes, bind.bytes_len);
      return Status::OK();
    case YBTHIN_BIND_BYTEA:
      out->set_binary_value(bind.bytes, bind.bytes_len);
      return Status::OK();
  }
  return STATUS_FORMAT(InvalidArgument, "bad bind tag $0", static_cast<int>(bind.tag));
}

Result<yb::QLOperator> MapCondOp(ybthin_cond_op op) {
  switch (op) {
    case YBTHIN_EQ: return yb::QL_OP_EQUAL;
    case YBTHIN_LE: return yb::QL_OP_LESS_THAN_EQUAL;
    case YBTHIN_GE: return yb::QL_OP_GREATER_THAN_EQUAL;
    case YBTHIN_LT: return yb::QL_OP_LESS_THAN;
    case YBTHIN_GT: return yb::QL_OP_GREATER_THAN;
  }
  return STATUS_FORMAT(InvalidArgument, "bad cond op $0", static_cast<int>(op));
}

Status BuildComparison(const ybthin_cond& c, yb::PgsqlExpressionPB* out) {
  auto* cond = out->mutable_condition();
  cond->set_op(VERIFY_RESULT(MapCondOp(c.op)));
  cond->add_operands()->set_column_id(c.column_id);
  return BindToQLValue(c.value, cond->add_operands()->mutable_value());
}

Result<ybthin_value_type> MapDataType(DataType dt) {
  switch (dt) {
    case DataType::BOOL: return YBTHIN_T_BOOL;
    case DataType::INT16: return YBTHIN_T_I16;
    case DataType::INT32: return YBTHIN_T_I32;
    case DataType::INT64: return YBTHIN_T_I64;
    case DataType::STRING: return YBTHIN_T_TEXT;
    case DataType::BINARY: return YBTHIN_T_BYTEA;
    default:
      return STATUS_FORMAT(NotSupported, "unsupported column data type $0", dt);
  }
}

Result<std::string> ReadFile(const char* path) {
  faststring data;
  RETURN_NOT_OK(yb::ReadFileToString(yb::Env::Default(), path, &data));
  return data.ToString();
}

// `create` opens a new session and returns its assigned id; otherwise `session_id` is kept alive.
Result<uint64_t> DoHeartbeat(
    const tserver::ThinClientServiceProxy& proxy, const MonoDelta& timeout, bool create,
    uint64_t session_id) {
  tserver::ThinHeartbeatRequestPB req;
  if (!create) {
    req.set_session_id(session_id);
  }
  tserver::ThinHeartbeatResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(timeout);
  RETURN_NOT_OK(proxy.Heartbeat(req, &resp, &controller));
  RETURN_NOT_OK(yb::ResponseStatus(resp));
  return resp.session_id();
}

Status OpenSession(ybthin_client& client, ybthin_session& session) REQUIRES(session.mutex) {
  auto& conn = *client.connections[session.conn_index];
  session.session_id = VERIFY_RESULT(
      DoHeartbeat(*conn.proxy, client.timeout, /* create= */ true, /* session_id= */ 0));
  ++session.generation;
  session.open = true;
  return Status::OK();
}

Status EnsureSessionOpen(ybthin_client& client, ybthin_session& session)
    REQUIRES(session.mutex) {
  return session.open ? Status::OK() : OpenSession(client, session);
}

// A network-class failure means the server may have dropped the session, so drop ours too and let
// the next use reopen it.
void MaybeMarkSessionDead(ybthin_session& session, const ybthin_status& status) {
  if (status.code == YBTHIN_NETWORK) {
    std::lock_guard<std::mutex> lock(session.mutex);
    session.open = false;
  }
}

size_t NextReadIndex(ybthin_client& client) {
  return client.read_session_cursor.fetch_add(1, std::memory_order_relaxed) %
         client.read_sessions.size();
}

// Falls back to the read sessions when the pool has no write sessions.
ybthin_session& NextWriteSession(ybthin_client& client) {
  auto& pool = client.write_sessions.empty() ? client.read_sessions : client.write_sessions;
  return *pool[client.write_session_cursor.fetch_add(1, std::memory_order_relaxed) % pool.size()];
}

// The shim's own envelope around the server's paging state, handed back to the caller opaquely:
//   [magic][version][u32 read-session index][u32 generation][u64 read_ht]
//   + the server's paging state.
// A continuation routes back to the same session (generation-checked) and replays the read time its
// scan was served at, so every page reads at one snapshot without the server tracking read points.
// Nothing in YugabyteDB defines this format -- pggate keeps the equivalent state in process memory
// and never serializes it. Version 3 replaced the read serial with the read time itself; bump the
// version for any further layout change.
constexpr uint8_t kPagingMagic = 0xB1;
constexpr uint8_t kPagingVersion = 3;
constexpr size_t kPagingPrefixLen = 2;  // magic + version
// Derived from the layout above so the writer, the reader and the length cannot drift apart.
constexpr size_t kPagingHeaderLen =
    kPagingPrefixLen + 2 * sizeof(uint32_t) + sizeof(uint64_t);

void AppendU32LE(std::string* out, uint32_t value) {
  char buf[sizeof(value)];
  LittleEndian::Store32(buf, value);
  out->append(buf, sizeof(buf));
}

void AppendU64LE(std::string* out, uint64_t value) {
  char buf[sizeof(value)];
  LittleEndian::Store64(buf, value);
  out->append(buf, sizeof(buf));
}

uint32_t ReadU32LE(const uint8_t** cursor) {
  const auto value = LittleEndian::Load32(*cursor);
  *cursor += sizeof(uint32_t);
  return value;
}

uint64_t ReadU64LE(const uint8_t** cursor) {
  const auto value = LittleEndian::Load64(*cursor);
  *cursor += sizeof(uint64_t);
  return value;
}

std::string WrapPagingState(
    uint32_t session_index, uint32_t generation, uint64_t read_ht,
    const std::string& server_paging_state) {
  std::string out;
  out.reserve(kPagingHeaderLen + server_paging_state.size());
  out.push_back(static_cast<char>(kPagingMagic));
  out.push_back(static_cast<char>(kPagingVersion));
  AppendU32LE(&out, session_index);
  AppendU32LE(&out, generation);
  AppendU64LE(&out, read_ht);
  out.append(server_paging_state);
  return out;
}

struct PinInfo {
  uint32_t session_index;
  uint32_t generation;
  // Replayed by a continuation so the server restores the scan's read point.
  uint64_t read_ht;
  Slice server_paging_state;
};

Result<PinInfo> UnwrapPagingState(const uint8_t* data, size_t len) {
  if (len < kPagingHeaderLen || data[0] != kPagingMagic || data[1] != kPagingVersion) {
    return STATUS(InvalidArgument, "malformed wrapped paging state");
  }
  const uint8_t* cursor = data + kPagingPrefixLen;
  PinInfo pin;
  pin.session_index = ReadU32LE(&cursor);
  pin.generation = ReadU32LE(&cursor);
  pin.read_ht = ReadU64LE(&cursor);
  pin.server_paging_state = Slice(cursor, data + len);
  return pin;
}

// Derives a read's partition key. A read is routed off its scan bounds rather than its
// range_column_values (only writes use those), so a supplied range key prefix is first encoded into
// bounds; a partial prefix widens to its whole range.
Status RouteRead(const ybthin_table& table, yb::PgsqlReadRequestPB* read) {
  // Derive the bounds from the REQUEST's prefix, not the caller's range_values: an Eq on the key
  // column after the prefix is folded into range_column_values above, and the deeper prefix is the
  // narrower range. Gating on the caller's count left a folded Eq with no bounds at all, so the
  // scan ran to the end of the table -- an empty page and a continuation per remaining tablet.
  if (!read->range_column_values().empty()) {
    auto lower = VERIFY_RESULT(dockv::GetRangeComponents(
        table.schema, read->range_column_values(), /* lower_bound= */ true));
    auto upper = VERIFY_RESULT(dockv::GetRangeComponents(
        table.schema, read->range_column_values(), /* lower_bound= */ false));
    auto* lb = read->mutable_lower_bound();
    lb->set_key(dockv::DocKey(std::move(lower)).Encode().ToStringBuffer());
    lb->set_is_inclusive(true);
    auto* ub = read->mutable_upper_bound();
    ub->set_key(dockv::DocKey(std::move(upper)).Encode().ToStringBuffer());
    ub->set_is_inclusive(true);
  }
  // InitHashPartitionKey's paging branch assumes the hash code bounds are already set, because
  // pggate reuses one request object for every page. We build a fresh one per page, so set them
  // here or a continuation would scan unbounded and re-read the first page.
  if (!read->partition_column_values().empty()) {
    const auto hash_code = VERIFY_RESULT(
        table.partition_schema.PgsqlHashColumnCompoundValue(read->partition_column_values()));
    read->set_hash_code(hash_code);
    read->set_max_hash_code(hash_code);
  }
  // No backward-scan adjustment here: the tserver already applies GetPartitionKeyForBackwardScan to
  // our (non-owning) ops off its own fresh tablet list, so doing it again would walk the key back
  // one tablet too far.
  return dockv::InitPartitionKey(table.schema, table.partition_schema, read);
}

std::string PgsqlResponseMessage(const yb::PgsqlResponsePB& resp) {
  if (resp.error_status_size() > 0) {
    return yb::StatusFromPB(resp.error_status(0)).ToString();
  }
  if (resp.has_error_message()) {
    return resp.error_message();
  }
  return "pgsql op failed";
}

ybthin_status PgsqlResponseError(const yb::PgsqlResponsePB& resp) {
  switch (resp.status()) {
    case yb::PgsqlResponsePB::PGSQL_STATUS_RESTART_REQUIRED_ERROR:
      return MakeStatus(YBTHIN_READ_RESTART, PgsqlResponseMessage(resp));
    case yb::PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH:
      return MakeStatus(YBTHIN_SCHEMA, PgsqlResponseMessage(resp));
    default:
      if (resp.error_status_size() > 0) {
        return FromStatus(yb::StatusFromPB(resp.error_status(0)));
      }
      return MakeStatus(YBTHIN_OTHER, PgsqlResponseMessage(resp));
  }
}

struct ReadCall {
  ybthin_session* session;  // the one session this batch runs on
  uint32_t session_index;   // its index into client->read_sessions
  uint32_t generation;      // session incarnation this batch ran at
  bool has_continuation;    // if set, the pinned session must still match
  // What the continuation ops were pinned to, and what this Perform actually ran at. The latter is
  // wrapped into the response paging state for the next page to replay.
  uint32_t pinned_generation;
  uint64_t pinned_read_ht;
  // The read time this batch was pinned to, either by the caller or, for a continuation, by the
  // scan. 0 means the server picked one, which FinishRead reads out of the response paging state.
  uint64_t read_ht;
  tserver::ThinPerformRequestPB req;
  tserver::ThinPerformResponsePB resp;
  rpc::RpcController controller;
  ybthin_read_cb cb;
  void* ctx;
  // Per-op target column types, in target (== response cell) order. The row sidecar is not
  // self-describing, so decoding needs them from the opened table's schema.
  std::vector<std::vector<ybthin_value_type>> op_target_types;
};

struct WriteCall {
  ybthin_session* session;
  tserver::ThinPerformRequestPB req;
  tserver::ThinPerformResponsePB resp;
  rpc::RpcController controller;
  ybthin_write_cb cb;
  void* ctx;
};

// Decodes the pg_doc_data row sidecar into a row-major array of (n_rows * target_types.size())
// cells, using YugabyteDB's own PgWire/PgDocData readers for the wire format. The cells and the
// TEXT/BYTEA payloads they point into are ONE malloc'd block (the byte arena trails the cell
// array), so the caller frees them with a single free(*out_cells).
Status DecodeReadRows(
    Slice sidecar, const std::vector<ybthin_value_type>& target_types,
    ybthin_cell** out_cells, size_t* out_n_rows) {
  int64_t row_count = 0;
  Slice cursor;
  pggate::PgDocData::LoadCache(sidecar, &row_count, &cursor);
  if (row_count < 0) {
    return STATUS_FORMAT(Corruption, "negative row count $0", row_count);
  }
  const size_t n_cols = target_types.size();
  const size_t n_cells = static_cast<size_t>(row_count) * n_cols;

  std::vector<ybthin_cell> cells(n_cells);  // value-initialized: tag 0 == NULL, bytes null
  std::string arena;                        // TEXT/BYTEA payloads, concatenated
  struct ByteRef { size_t cell_idx; size_t off; size_t len; };
  std::vector<ByteRef> byte_refs;

  for (int64_t row = 0; row < row_count; ++row) {
    for (size_t col = 0; col < n_cols; ++col) {
      const size_t idx = static_cast<size_t>(row) * n_cols + col;
      ybthin_cell& cell = cells[idx];
      if (VERIFY_RESULT(pggate::PgDocData::CheckedReadHeaderIsNull(&cursor))) {
        cell.tag = YBTHIN_BIND_NULL;
        continue;
      }
      switch (target_types[col]) {
        case YBTHIN_T_BOOL:
          cell.tag = YBTHIN_BIND_BOOL;
          cell.int_value = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<uint8_t>(&cursor));
          break;
        case YBTHIN_T_I16:
          cell.tag = YBTHIN_BIND_I16;
          cell.int_value = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<int16_t>(&cursor));
          break;
        case YBTHIN_T_I32:
          cell.tag = YBTHIN_BIND_I32;
          cell.int_value = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<int32_t>(&cursor));
          break;
        case YBTHIN_T_I64:
          cell.tag = YBTHIN_BIND_I64;
          cell.int_value = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<int64_t>(&cursor));
          break;
        case YBTHIN_T_TEXT: {
          // Length-prefixed and NUL-terminated: len counts the trailing NUL.
          const uint64_t len = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<uint64_t>(&cursor));
          if (len == 0) {
            return STATUS(Corruption, "TEXT cell with zero length (missing NUL)");
          }
          if (cursor.size() < len) {
            return STATUS(Corruption, "TEXT cell truncated");
          }
          cell.tag = YBTHIN_BIND_TEXT;
          byte_refs.push_back({idx, arena.size(), len - 1});
          arena.append(cursor.cdata(), len - 1);  // value bytes, excluding the trailing NUL
          cursor.remove_prefix(len);               // consume value + NUL
          break;
        }
        case YBTHIN_T_BYTEA: {
          const uint64_t len = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<uint64_t>(&cursor));
          if (cursor.size() < len) {
            return STATUS(Corruption, "BYTEA cell truncated");
          }
          cell.tag = YBTHIN_BIND_BYTEA;
          byte_refs.push_back({idx, arena.size(), len});
          arena.append(cursor.cdata(), len);
          cursor.remove_prefix(len);
          break;
        }
      }
    }
  }
  if (!cursor.empty()) {
    return STATUS_FORMAT(Corruption, "trailing garbage: $0 bytes after $1 rows",
                         cursor.size(), row_count);
  }

  const size_t cells_bytes = n_cells * sizeof(ybthin_cell);
  const size_t total = cells_bytes + arena.size();
  auto* block = static_cast<uint8_t*>(malloc(total ? total : 1));
  if (!block) {
    return STATUS(RuntimeError, "out of memory decoding read rows");
  }
  auto* out = reinterpret_cast<ybthin_cell*>(block);
  if (cells_bytes) {
    memcpy(out, cells.data(), cells_bytes);
  }
  uint8_t* arena_base = block + cells_bytes;
  if (!arena.empty()) {
    memcpy(arena_base, arena.data(), arena.size());
  }
  for (const auto& ref : byte_refs) {
    out[ref.cell_idx].bytes = arena_base + ref.off;
    out[ref.cell_idx].bytes_len = ref.len;
  }
  *out_cells = out;
  *out_n_rows = static_cast<size_t>(row_count);
  return Status::OK();
}

void FinishRead(ReadCall* read_call) {
  std::unique_ptr<ReadCall> call(read_call);

  Status rpc_status = call->controller.status();
  if (!rpc_status.ok()) {
    auto st = FromStatus(rpc_status);
    MaybeMarkSessionDead(*call->session, st);
    call->cb(call->ctx, st, nullptr);
    return;
  }
  Status app_status = yb::ResponseStatus(call->resp);
  if (!app_status.ok()) {
    auto st = FromStatus(app_status);
    MaybeMarkSessionDead(*call->session, st);
    call->cb(call->ctx, st, nullptr);
    return;
  }
  const size_t n_ops = call->op_target_types.size();
  if (static_cast<size_t>(call->resp.responses_size()) != n_ops) {
    call->cb(call->ctx, MakeStatus(YBTHIN_OTHER, "Perform op-response count mismatch"), nullptr);
    return;
  }
  // A Perform fails/read-restarts as a unit, so any op error fails the whole call with no partial
  // results.
  for (size_t op_idx = 0; op_idx < n_ops; ++op_idx) {
    const auto& op = call->resp.responses(static_cast<int>(op_idx));
    if (op.status() != yb::PgsqlResponsePB::PGSQL_STATUS_OK) {
      call->cb(call->ctx, PgsqlResponseError(op), nullptr);
      return;
    }
  }

  auto* result = static_cast<ybthin_read_result*>(calloc(1, sizeof(ybthin_read_result)));
  result->n_ops = n_ops;
  result->results = static_cast<ybthin_read_op_result*>(
      calloc(n_ops ? n_ops : 1, sizeof(ybthin_read_op_result)));

  // A fresh batch's snapshot is reported in the paging state; surface it so the caller can pin
  // follow-up batches to the same one.
  uint64_t picked_read_ht = 0;

  for (size_t op_idx = 0; op_idx < n_ops; ++op_idx) {
    const auto& op = call->resp.responses(static_cast<int>(op_idx));
    auto& op_result = result->results[op_idx];
    op_result.n_cols = call->op_target_types[op_idx].size();
    if (op.has_rows_data_sidecar()) {
      auto sidecar = call->controller.ExtractSidecar(op.rows_data_sidecar());
      if (!sidecar.ok()) {
        ybthin_read_result_free(result);
        call->cb(call->ctx, FromStatus(sidecar.status()), nullptr);
        return;
      }
      ybthin_cell* cells = nullptr;
      size_t n_rows = 0;
      Status decoded =
          DecodeReadRows(sidecar->AsSlice(), call->op_target_types[op_idx], &cells, &n_rows);
      if (!decoded.ok()) {
        ybthin_read_result_free(result);
        call->cb(call->ctx, FromStatus(decoded), nullptr);
        return;
      }
      op_result.cells = cells;
      op_result.n_rows = n_rows;
    }
    // No paging_state means the scan is exhausted. Otherwise add the pinning header, so a
    // continuation returns to this batch's session.
    if (op.has_paging_state()) {
      if (op.paging_state().read_time().has_read_ht()) {
        picked_read_ht = op.paging_state().read_time().read_ht();
      }
      // The next page has to read at this page's snapshot: the time we pinned, or the one the
      // server picked and reported here.
      const uint64_t scan_read_ht = call->read_ht != 0 ? call->read_ht : picked_read_ht;
      std::string srv;
      op.paging_state().SerializeToString(&srv);
      std::string wrapped = WrapPagingState(
          call->session_index, call->generation, scan_read_ht, srv);
      op_result.paging_state = DupBytes(wrapped.data(), wrapped.size());
      op_result.paging_state_len = wrapped.size();
    }
  }
  // Pinned batch: echo the time we sent. Fresh batch: report the server-picked one.
  result->used_read_time_ht = call->read_ht != 0 ? call->read_ht : picked_read_ht;
  call->cb(call->ctx, OkStatus(), result);
}

void FinishWrite(WriteCall* write_call) {
  std::unique_ptr<WriteCall> call(write_call);

  Status rpc_status = call->controller.status();
  if (!rpc_status.ok()) {
    auto st = FromStatus(rpc_status);
    MaybeMarkSessionDead(*call->session, st);
    call->cb(call->ctx, st);
    return;
  }
  Status app_status = yb::ResponseStatus(call->resp);
  if (!app_status.ok()) {
    // A replay the server already replicated is durable, so the batch succeeded -- see
    // IsAlreadyReplicatedWrite. Reporting it as an error is what turns a caller's retry into a
    // permanent loop.
    if (IsAlreadyReplicatedWrite(app_status)) {
      call->cb(call->ctx, OkStatus());
      return;
    }
    auto st = FromStatus(app_status);
    MaybeMarkSessionDead(*call->session, st);
    call->cb(call->ctx, st);
    return;
  }
  // A dropped row is an error, never a silent short write.
  for (const auto& op : call->resp.responses()) {
    if (op.status() != yb::PgsqlResponsePB::PGSQL_STATUS_OK) {
      if (op.error_status_size() > 0 &&
          IsAlreadyReplicatedWrite(yb::StatusFromPB(op.error_status(0)))) {
        continue;  // this row's write is already durable
      }
      call->cb(call->ctx, PgsqlResponseError(op));
      return;
    }
  }
  call->cb(call->ctx, OkStatus());
}

}  // namespace

extern "C" {

ybthin_status ybthin_client_create(
    const char* const* tserver_addrs, size_t n_addrs, const ybthin_tls_opts* tls,
    const ybthin_pool_opts* pool, uint32_t rpc_timeout_ms, uint32_t num_reactors,
    ybthin_client** out) {
  if (!tserver_addrs || n_addrs == 0 || !out) {
    return MakeStatus(YBTHIN_INVALID, "tserver_addrs and out are required");
  }

  const uint32_t read_n =
      (pool && pool->read_sessions) ? pool->read_sessions : kDefaultReadSessions;
  const uint32_t write_n =
      (pool && pool->write_sessions) ? pool->write_sessions : kDefaultWriteSessions;
  const uint32_t per_conn =
      (pool && pool->sessions_per_conn) ? pool->sessions_per_conn : kDefaultSessionsPerConn;
  if (read_n == 0) {
    return MakeStatus(YBTHIN_INVALID, "read_sessions must be >= 1");
  }
  const uint32_t num_conns = (read_n + write_n + per_conn - 1) / per_conn;

  auto client = std::make_unique<ybthin_client>();
  client->timeout =
      MonoDelta::FromMilliseconds(rpc_timeout_ms ? rpc_timeout_ms : kDefaultRpcTimeoutMs);
  client->num_reactors = num_reactors ? static_cast<int>(num_reactors) : kDefaultNumReactors;

  for (size_t addr_idx = 0; addr_idx < n_addrs; ++addr_idx) {
    auto host_port = HostPort::FromString(tserver_addrs[addr_idx], kDefaultTserverRpcPort);
    if (host_port.ok()) {
      client->hosts.push_back(*host_port);
    }
  }
  if (client->hosts.empty()) {
    return MakeStatus(YBTHIN_INVALID, "no valid tserver addresses");
  }

  if (tls && tls->ca_cert_path) {
    const bool mtls = tls->cert_path && tls->key_path;
    auto secure_context = std::make_unique<rpc::SecureContext>(
        rpc::RequireClientCertificate::kFalse, rpc::UseClientCertificate(mtls));
    if (mtls) {
      auto cert = ReadFile(tls->cert_path);
      if (!cert.ok()) {
        return FromStatus(cert.status());
      }
      auto key = ReadFile(tls->key_path);
      if (!key.ok()) {
        return FromStatus(key.status());
      }
      Status status = secure_context->UseCertificates(
          tls->ca_cert_path, Slice(*cert), Slice(*key));
      if (!status.ok()) {
        return FromStatus(status);
      }
    } else {
      Status status = secure_context->AddCertificateAuthorityFile(tls->ca_cert_path);
      if (!status.ok()) {
        return FromStatus(status);
      }
    }
    client->secure_context = std::move(secure_context);
  }

  auto shutdown_all = [&client] {
    for (auto& conn : client->connections) {
      if (conn->messenger) {
        conn->messenger->Shutdown();
      }
    }
  };

  for (uint32_t conn_idx = 0; conn_idx < num_conns; ++conn_idx) {
    auto conn = std::make_unique<ybthin_connection>();
    rpc::MessengerBuilder builder("yb_thin_client");
    builder.set_num_reactors(client->num_reactors);
    builder.UseDefaultConnectionContextFactory();
    if (client->secure_context) {
      rpc::ApplySecureContext(client->secure_context.get(), &builder);
    }
    auto messenger = builder.Build();
    if (!messenger.ok()) {
      shutdown_all();
      return FromStatus(messenger.status());
    }
    conn->messenger = std::move(*messenger);
    conn->proxy_cache = std::make_unique<rpc::ProxyCache>(conn->messenger.get());
    conn->host = client->hosts[conn_idx % client->hosts.size()];
    conn->proxy =
        std::make_unique<tserver::ThinClientServiceProxy>(conn->proxy_cache.get(), conn->host);
    client->connections.push_back(std::move(conn));
  }

  // Pack sessions_per_conn sessions per connection: session N lands on connection N / per_conn.
  uint32_t session_index = 0;
  auto add_session = [&](std::vector<std::unique_ptr<ybthin_session>>* dst) -> Status {
    auto session = std::make_unique<ybthin_session>();
    session->conn_index = session_index / per_conn;
    std::lock_guard<std::mutex> lock(session->mutex);
    RETURN_NOT_OK(OpenSession(*client, *session));
    dst->push_back(std::move(session));
    ++session_index;
    return Status::OK();
  };
  for (uint32_t read_idx = 0; read_idx < read_n; ++read_idx) {
    Status status = add_session(&client->read_sessions);
    if (!status.ok()) {
      shutdown_all();
      return FromStatus(status);
    }
  }
  for (uint32_t write_idx = 0; write_idx < write_n; ++write_idx) {
    Status status = add_session(&client->write_sessions);
    if (!status.ok()) {
      shutdown_all();
      return FromStatus(status);
    }
  }

  // A failed heartbeat drops the session, so the next use reopens it.
  ybthin_client* client_ptr = client.get();
  client_ptr->keepalive_thread = std::thread([client_ptr] {  // NOLINT(build/std_thread)
    std::unique_lock<std::mutex> lock(client_ptr->mutex);
    while (!client_ptr->stop) {
      if (client_ptr->cv.wait_for(lock, std::chrono::milliseconds(kKeepaliveIntervalMs),
                           [client_ptr] { return client_ptr->stop; })) {
        break;
      }
      lock.unlock();
      const auto heartbeat_timeout = std::min(client_ptr->timeout, MonoDelta::FromSeconds(5));
      auto ping = [client_ptr, heartbeat_timeout](ybthin_session& session) {
        std::lock_guard<std::mutex> session_lock(session.mutex);
        if (!session.open) {
          return;
        }
        auto result = DoHeartbeat(
            *client_ptr->connections[session.conn_index]->proxy, heartbeat_timeout,
            /* create= */ false, session.session_id);
        if (!result.ok()) {
          session.open = false;
        }
      };
      for (auto& session : client_ptr->read_sessions) {
        ping(*session);
      }
      for (auto& session : client_ptr->write_sessions) {
        ping(*session);
      }
      lock.lock();
    }
  });

  *out = client.release();
  return OkStatus();
}

void ybthin_client_destroy(ybthin_client* client) {
  if (!client) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(client->mutex);
    client->stop = true;
  }
  client->cv.notify_all();
  if (client->keepalive_thread.joinable()) {
    client->keepalive_thread.join();
  }
  for (auto& conn : client->connections) {
    if (conn->messenger) {
      conn->messenger->Shutdown();
    }
  }
  delete client;
}

ybthin_status ybthin_table_open(
    ybthin_client* client, uint32_t db_oid, uint32_t table_oid, ybthin_table** out,
    ybthin_table_info* info_out) {
  if (!client || !out || !info_out) {
    return MakeStatus(YBTHIN_INVALID, "client, out and info_out are required");
  }
  auto table = std::make_unique<ybthin_table>();
  table->table_id = yb::GetPgsqlTableId(db_oid, table_oid);

  tserver::ThinOpenTableRequestPB req;
  req.set_table_id(table->table_id);
  tserver::ThinOpenTableResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(client->timeout);

  // OpenTable is session-less; any connection serves it.
  Status status = client->connections.front()->proxy->OpenTable(req, &resp, &controller);
  if (!status.ok()) {
    return FromStatus(status);
  }
  status = yb::ResponseStatus(resp);
  if (!status.ok()) {
    return FromStatus(status);
  }

  const auto& info = resp.info();
  table->schema_version = info.version();
  status = yb::SchemaFromPB(info.schema(), &table->schema);
  if (!status.ok()) {
    return FromStatus(status);
  }
  status = PartitionSchema::FromPB(
      info.partition_schema(), table->schema, &table->partition_schema);
  if (!status.ok()) {
    return FromStatus(status);
  }

  const size_t num_hash_key_columns = table->schema.num_hash_key_columns();
  const size_t num_key_columns = table->schema.num_key_columns();
  const size_t num_columns = table->schema.num_columns();
  table->columns.reserve(num_columns);
  for (size_t col_idx = 0; col_idx < num_columns; ++col_idx) {
    const auto& col = table->schema.column(col_idx);
    auto type = MapDataType(col.type()->main());
    if (!type.ok()) {
      return FromStatus(type.status());
    }
    ybthin_col_kind kind = col_idx < num_hash_key_columns ? YBTHIN_COL_HASH
                           : col_idx < num_key_columns    ? YBTHIN_COL_RANGE
                                                    : YBTHIN_COL_VALUE;
    table->columns.push_back(
        {col.name(), table->schema.column_id(col_idx).rep(), kind, *type});
  }

  auto* out_cols = static_cast<ybthin_column*>(malloc(num_columns * sizeof(ybthin_column)));
  for (size_t col_idx = 0; col_idx < num_columns; ++col_idx) {
    const auto& ci = table->columns[col_idx];
    out_cols[col_idx] = ybthin_column{ci.name.c_str(), ci.id, ci.kind, ci.type};
  }
  info_out->columns = out_cols;
  info_out->n_columns = num_columns;

  *out = table.release();
  return OkStatus();
}

void ybthin_table_close(ybthin_table* table) { delete table; }

void ybthin_columns_free(ybthin_column* columns, size_t /* n */) {
  // Only the array is shim-heap; the name strings are owned by the ybthin_table handle.
  free(columns);
}

void ybthin_read_async(
    ybthin_client* client, const ybthin_read_op* ops, size_t n_ops, uint64_t read_time_ht,
    ybthin_read_cb cb, void* ctx) {
  if (n_ops == 0) {
    cb(ctx, MakeStatus(YBTHIN_INVALID, "read batch has no ops"), nullptr);
    return;
  }
  auto call = std::make_unique<ReadCall>();
  call->cb = cb;
  call->ctx = ctx;
  call->read_ht = read_time_ht;

  // A continuation must return to the session that issued it, so every one in a batch has to pin
  // the same session. A batch with no continuations round-robins the read pool.
  bool have_pinned = false;
  size_t session_index = 0;
  uint32_t pinned_generation = 0;
  uint64_t pinned_read_ht = 0;
  std::vector<Slice> server_paging_states(n_ops);  // empty entry => fresh op
  for (size_t op_idx = 0; op_idx < n_ops; ++op_idx) {
    const auto& op = ops[op_idx];
    if (!(op.paging_state_in && op.paging_state_in_len > 0)) {
      continue;
    }
    auto pin = UnwrapPagingState(op.paging_state_in, op.paging_state_in_len);
    if (!pin.ok()) {
      cb(ctx, MakeStatus(YBTHIN_INVALID, "could not parse paging_state_in"), nullptr);
      return;
    }
    if (pin->session_index >= client->read_sessions.size()) {
      cb(ctx, MakeStatus(YBTHIN_READ_RESTART, "pinned read session no longer exists"), nullptr);
      return;
    }
    if (!have_pinned) {
      have_pinned = true;
      session_index = pin->session_index;
      pinned_generation = pin->generation;
      pinned_read_ht = pin->read_ht;
    } else if (pin->session_index != session_index || pin->generation != pinned_generation ||
               pin->read_ht != pinned_read_ht) {
      // One Perform carries one read time, hence one snapshot.
      cb(ctx, MakeStatus(
             YBTHIN_INVALID, "all continuation ops in a batch must share the paging session"),
         nullptr);
      return;
    }
    server_paging_states[op_idx] = pin->server_paging_state;
  }
  if (!have_pinned) {
    session_index = NextReadIndex(*client);
  }
  call->session = client->read_sessions[session_index].get();
  call->session_index = static_cast<uint32_t>(session_index);
  call->has_continuation = have_pinned;
  call->pinned_generation = pinned_generation;
  call->pinned_read_ht = pinned_read_ht;

  // One Perform with n_ops read requests, in caller order -- results map back by index.
  auto& req = call->req;
  call->op_target_types.resize(n_ops);
  Status build = Status::OK();
  for (size_t op_idx = 0; op_idx < n_ops; ++op_idx) {
    const ybthin_table* table = ops[op_idx].table;
    const ybthin_read_spec* spec = &ops[op_idx].spec;
    // The wrong kind of key would leave the partition key unset, quietly reading only the first
    // tablet.
    const bool range_sharded = table->schema.num_hash_key_columns() == 0;
    if (range_sharded && spec->n_hash > 0) {
      cb(ctx, MakeStatus(
             YBTHIN_INVALID, "table is range-sharded: use spec.range_values, not hash_values"),
         nullptr);
      return;
    }
    if (!range_sharded && spec->n_range > 0) {
      cb(ctx, MakeStatus(
             YBTHIN_INVALID, "table is hash-sharded: use spec.hash_values, not range_values"),
         nullptr);
      return;
    }
    if (spec->n_hash > table->schema.num_hash_key_columns() ||
        spec->n_range > table->schema.num_range_key_columns()) {
      cb(ctx, MakeStatus(YBTHIN_INVALID, "more key values than key columns"), nullptr);
      return;
    }
    // A range key PREFIX is a legitimate partial key -- a hash one is not. The hash code is
    // computed over the hash columns as a group, so a short list hashes to an unrelated tablet and
    // the scan quietly reads the wrong one (DocDB only DCHECKs the count). Bind all or none.
    if (spec->n_hash != 0 && spec->n_hash != table->schema.num_hash_key_columns()) {
      cb(ctx, MakeStatus(
             YBTHIN_INVALID, "hash_values must bind every hash key column, or none of them"),
         nullptr);
      return;
    }
    // DocDB takes range_column_values as the range key prefix and forbids nulls in it; binding one
    // would silently change which rows the prefix addresses.
    for (size_t range_idx = 0; range_idx < spec->n_range; ++range_idx) {
      if (spec->range_values[range_idx].tag == YBTHIN_BIND_NULL) {
        cb(ctx, MakeStatus(YBTHIN_INVALID, "range key values may not be null"), nullptr);
        return;
      }
    }

    auto* read = req.add_ops()->mutable_read();
    read->set_client(yb::YQL_CLIENT_PGSQL);
    read->set_table_id(table->table_id);
    read->set_schema_version(table->schema_version);

    for (size_t hash_idx = 0; hash_idx < spec->n_hash && build.ok(); ++hash_idx) {
      build = BindToQLValue(
          spec->hash_values[hash_idx], read->add_partition_column_values()->mutable_value());
    }
    for (size_t range_idx = 0; range_idx < spec->n_range && build.ok(); ++range_idx) {
      build = BindToQLValue(
          spec->range_values[range_idx], read->add_range_column_values()->mutable_value());
    }
    // An Eq on the key column right after the prefix EXTENDS the prefix; it is not a filter. Left
    // in condition_expr it would contradict the scan range DocDB derives from the prefix, and the
    // scan would return nothing. pggate normalizes the same way (PgDmlRead::ProcessEmptyKeyBinds
    // moves conditions to condition_expr only once a PRECEDING key column is unbound). Non-Eq
    // conditions, and anything past the first unbound column, stay filters.
    std::vector<bool> cond_folded(spec->n_conds, false);
    if (table->schema.num_hash_key_columns() == 0) {
      const size_t num_key_columns = table->schema.num_key_columns();
      for (size_t key_idx = spec->n_range; build.ok() && key_idx < num_key_columns; ++key_idx) {
        const int32_t key_col_id = table->columns[key_idx].id;
        size_t match = spec->n_conds;
        for (size_t cond_idx = 0; cond_idx < spec->n_conds; ++cond_idx) {
          if (!cond_folded[cond_idx] && spec->conds[cond_idx].column_id == key_col_id &&
              spec->conds[cond_idx].op == YBTHIN_EQ &&
              spec->conds[cond_idx].value.tag != YBTHIN_BIND_NULL) {
            match = cond_idx;
            break;
          }
        }
        if (match == spec->n_conds) {
          break;  // gap in the key prefix: everything from here on stays a filter
        }
        build = BindToQLValue(
            spec->conds[match].value, read->add_range_column_values()->mutable_value());
        cond_folded[match] = true;
      }
    }
    size_t n_filters = 0;
    for (size_t cond_idx = 0; cond_idx < spec->n_conds; ++cond_idx) {
      if (!cond_folded[cond_idx]) {
        ++n_filters;
      }
    }
    if (build.ok() && n_filters > 0) {
      if (n_filters == 1) {
        for (size_t cond_idx = 0; cond_idx < spec->n_conds && build.ok(); ++cond_idx) {
          if (!cond_folded[cond_idx]) {
            build = BuildComparison(spec->conds[cond_idx], read->mutable_condition_expr());
          }
        }
      } else {
        auto* cond = read->mutable_condition_expr()->mutable_condition();
        cond->set_op(yb::QL_OP_AND);
        for (size_t cond_idx = 0; cond_idx < spec->n_conds && build.ok(); ++cond_idx) {
          if (!cond_folded[cond_idx]) {
            build = BuildComparison(spec->conds[cond_idx], cond->add_operands());
          }
        }
      }
    }
    if (!build.ok()) {
      cb(ctx, FromStatus(build), nullptr);
      return;
    }

    auto& types = call->op_target_types[op_idx];
    types.reserve(spec->n_targets);
    for (size_t target_idx = 0; target_idx < spec->n_targets; ++target_idx) {
      read->add_targets()->set_column_id(spec->target_ids[target_idx]);
      read->add_col_refs()->set_column_id(spec->target_ids[target_idx]);
      // The sidecar carries values but no types, so resolve them from the opened schema.
      const ybthin_value_type* type = nullptr;
      for (const auto& col : table->columns) {
        if (col.id == spec->target_ids[target_idx]) {
          type = &col.type;
          break;
        }
      }
      if (!type) {
        cb(ctx, MakeStatus(YBTHIN_INVALID, "read target column id is not in the table"), nullptr);
        return;
      }
      types.push_back(*type);
    }
    read->set_is_forward_scan(spec->is_forward_scan != 0);
    if (spec->limit) {
      read->set_limit(spec->limit);
    }
    read->set_return_paging_state(true);
    if (!server_paging_states[op_idx].empty()) {
      const Slice& ps = server_paging_states[op_idx];
      if (!read->mutable_paging_state()->ParseFromArray(
              ps.data(), static_cast<int>(ps.size()))) {
        cb(ctx, MakeStatus(YBTHIN_INVALID, "could not parse paging_state_in"), nullptr);
        return;
      }
    }
    // Route last: the partition key depends on the scan direction, the bounds and the paging state.
    build = RouteRead(*table, read);
    if (!build.ok()) {
      cb(ctx, FromStatus(build), nullptr);
      return;
    }
  }

  // One snapshot for the whole batch: the caller's explicit read time if given, otherwise the
  // scan's own (set below for a continuation), otherwise none and the server picks one.
  auto* rto = req.mutable_read_time_options();
  if (read_time_ht != 0) {
    // An explicit snapshot, e.g. a prior batch's used_read_time_ht.
    auto* rt = rto->mutable_read_time();
    rt->set_read_ht(read_time_ht);
    rt->set_global_limit_ht(read_time_ht);
  }

  // A continuation whose session was reopened can no longer be served, so report READ_RESTART.
  ReadCall* read_call = call.release();
  auto& session = *read_call->session;
  ybthin_status early = OkStatus();
  bool dispatch = false;
  {
    std::lock_guard<std::mutex> lock(session.mutex);
    Status open = EnsureSessionOpen(*client, session);
    if (!open.ok()) {
      early = FromStatus(open);
    } else if (read_call->has_continuation && session.generation != read_call->pinned_generation) {
      early = MakeStatus(YBTHIN_READ_RESTART, "pinned read session was reopened");
    } else {
      read_call->generation = session.generation;
      // A continuation replays the read time its scan was served at, so every page reads at that
      // one snapshot. A fresh scan sends no read time and the server picks one, which it reports
      // back in the paging state. Either way the server keeps no read state of its own.
      if (read_call->read_ht == 0 && read_call->has_continuation &&
          read_call->pinned_read_ht != 0) {
        read_call->read_ht = read_call->pinned_read_ht;
        auto* rt = rto->mutable_read_time();
        rt->set_read_ht(read_call->read_ht);
        rt->set_global_limit_ht(read_call->read_ht);
      }
      for (auto& op : *read_call->req.mutable_ops()) {
        op.mutable_read()->set_stmt_id(session.stmt_id++);
      }
      read_call->req.set_session_id(session.session_id);
      dispatch = true;
    }
  }
  if (!dispatch) {
    std::unique_ptr<ReadCall> deleter(read_call);
    cb(ctx, early, nullptr);
    return;
  }
  read_call->controller.set_timeout(client->timeout);
  client->connections[session.conn_index]->proxy->PerformAsync(
      read_call->req, &read_call->resp, &read_call->controller,
      [read_call] { FinishRead(read_call); });
}

void ybthin_read_result_free(ybthin_read_result* result) {
  if (!result) {
    return;
  }
  if (result->results) {
    for (size_t op_idx = 0; op_idx < result->n_ops; ++op_idx) {
      // Each op's `cells` is a single block (cell array + trailing TEXT/BYTEA byte arena).
      free(result->results[op_idx].cells);
      free(result->results[op_idx].paging_state);
    }
    free(result->results);
  }
  free(result);
}

void ybthin_upsert_batch_async(
    ybthin_client* client, const ybthin_upsert_row* rows, size_t n_rows,
    ybthin_write_cb cb, void* ctx) {
  if (n_rows == 0) {
    cb(ctx, MakeStatus(YBTHIN_INVALID, "upsert batch has no rows"));
    return;
  }
  auto call = std::make_unique<WriteCall>();
  call->cb = cb;
  call->ctx = ctx;
  call->session = &NextWriteSession(*client);

  auto& req = call->req;

  Status build = Status::OK();
  for (size_t row_idx = 0; row_idx < n_rows && build.ok(); ++row_idx) {
    const auto& row = rows[row_idx];
    const ybthin_table* table = row.table;
    // A short primary key is padded out and hits a different row.
    if (row.n_keys != table->schema.num_key_columns()) {
      cb(ctx, MakeStatus(YBTHIN_INVALID, "upsert row must bind every primary key column"));
      return;
    }
    auto* write = req.add_ops()->mutable_write();
    write->set_client(yb::YQL_CLIENT_PGSQL);
    write->set_stmt_type(yb::PgsqlWriteRequestPB::PGSQL_UPSERT);
    write->set_table_id(table->table_id);
    write->set_schema_version(table->schema_version);

    // Key columns arrive in schema order: hash columns first, then range columns.
    const size_t num_hash_key_columns = table->schema.num_hash_key_columns();
    for (size_t col_idx = 0; col_idx < row.n_keys && build.ok(); ++col_idx) {
      auto* expr = col_idx < num_hash_key_columns ? write->add_partition_column_values()
                                            : write->add_range_column_values();
      build = BindToQLValue(row.key_values[col_idx], expr->mutable_value());
    }
    if (build.ok()) {
      build = dockv::InitPartitionKey(table->schema, table->partition_schema, write);
    }
    if (row.ignore_after_hybrid_time) {
      write->set_ignore_after_hybrid_time(row.ignore_after_hybrid_time);
    }
    for (size_t col_idx = 0; col_idx < row.n_values && build.ok(); ++col_idx) {
      auto* cv = write->add_column_values();
      cv->set_column_id(row.value_ids[col_idx]);
      build = BindToQLValue(row.values[col_idx], cv->mutable_expr()->mutable_value());
    }
  }
  if (!build.ok()) {
    cb(ctx, FromStatus(build));
    return;
  }

  // A write carries a fresh read-time serial and no read time, so the storage layer picks the
  // time for each op.
  WriteCall* write_call = call.release();
  auto& session = *write_call->session;
  ybthin_status early = OkStatus();
  bool dispatch = false;
  {
    std::lock_guard<std::mutex> lock(session.mutex);
    Status open = EnsureSessionOpen(*client, session);
    if (!open.ok()) {
      early = FromStatus(open);
    } else {
      for (auto& op : *write_call->req.mutable_ops()) {
        op.mutable_write()->set_stmt_id(session.stmt_id++);
      }
      write_call->req.set_session_id(session.session_id);
      dispatch = true;
    }
  }
  if (!dispatch) {
    std::unique_ptr<WriteCall> deleter(write_call);
    cb(ctx, early);
    return;
  }
  write_call->controller.set_timeout(client->timeout);
  client->connections[session.conn_index]->proxy->PerformAsync(
      write_call->req, &write_call->resp, &write_call->controller,
      [write_call] { FinishWrite(write_call); });
}

void ybthin_string_free(char* str) { free(str); }

}  // extern "C"
