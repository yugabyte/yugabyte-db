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
// yb_thin_client.cc — a thin, Perform-based YugabyteDB tserver client.
//
// Implements the C ABI in yb_thin_client.h by speaking yb.tserver.PgClientService.Perform
// directly to a tserver RPC endpoint via PgClientServiceProxy. There is deliberately NO YBClient /
// YBSession / Batcher / metacache / TabletInvoker: each op carries its own partition routing info
// (hash_code + partition_key) and the tserver routes it, exactly as it does for pggate.
//
// BACKWARD COMPATIBILITY: this library is shipped and consumed outside the YugabyteDB build and is
// upgraded before the tserver, so it must keep working against an OLDER tserver than it was
// compiled against. Keep the wire usage backward-compatible: only send request fields / rely on
// RPCs and semantics the oldest supported tserver understands, and tolerate responses that lack
// fields a newer server would set. Keep the C ABI (see yb_thin_client.h) additive-only.
//

#include "yb/thin_client/yb_thin_client.h"

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "yb/common/common.pb.h"
#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/ql_type.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/value.pb.h"
#include "yb/common/wire_protocol.h"

#include "yb/dockv/partition.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/secure.h"
#include "yb/rpc/secure_stream.h"

#include "yb/tserver/pg_client.pb.h"
#include "yb/tserver/pg_client.proxy.h"

#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pggate/util/pg_wire.h"

#include "yb/util/monotime.h"
#include "yb/util/net/net_util.h"
#include "yb/util/ref_cnt_buffer.h"
#include "yb/util/result.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

using yb::DataType;
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

// Default tserver RPC port used when a "host" is passed without ":port".
static constexpr uint16_t kDefaultTserverRpcPort = 9100;
// Session keepalive cadence — well inside the server's default expiry window.
static constexpr int kKeepaliveIntervalMs = 10000;
static constexpr int kDefaultRpcTimeoutMs = 60000;
static constexpr int kDefaultNumReactors = 4;
// Default pool shape (see ybthin_pool_opts): concurrency from multiple sessions, packed 4 per
// connection so a small pool opens a single connection.
static constexpr uint32_t kDefaultReadSessions = 4;
static constexpr uint32_t kDefaultWriteSessions = 1;
static constexpr uint32_t kDefaultSessionsPerConn = 4;

// ---- opaque handles -------------------------------------------------------

// One connection to a tserver: its own messenger (hence its own socket, so N connections spread
// across tserver nodes behind a ClusterIP VIP) and a PgClientServiceProxy. Sessions are packed onto
// connections.
struct ybthin_connection {
  std::unique_ptr<rpc::Messenger> messenger;
  std::unique_ptr<rpc::ProxyCache> proxy_cache;
  std::unique_ptr<tserver::PgClientServiceProxy> proxy;
  HostPort host;
};

// One PgClientService session. The server serializes a session's Performs by a contiguous serial
// starting at 0 (pg_client_service.cc next_expected_serial_no_), so serial_no / read_time_serial /
// stmt_id are per-session and serial_no MUST start at 0 and be allocated only for dispatched
// Performs (a gap stalls later Performs until their deadline). `generation` identifies this
// incarnation: a paged scan pinned to (session, generation) becomes invalid once the session is
// reopened with a fresh session_id (serials reset), so continuations on a stale generation are
// reported as YBTHIN_READ_RESTART.
struct ybthin_session {
  size_t conn_index = 0;
  std::mutex mu;  // guards {open, session_id, serial_no, read_time_serial, stmt_id, generation}
  bool open = false;
  uint32_t generation = 0;
  uint64_t session_id = 0;
  uint64_t serial_no = 0;
  uint64_t read_time_serial = 1;
  uint64_t stmt_id = 1;
};

struct ybthin_client {
  std::unique_ptr<rpc::SecureContext> secure_context;  // null for plaintext; shared by all conns
  std::vector<std::unique_ptr<ybthin_connection>> connections;
  std::vector<std::unique_ptr<ybthin_session>> read_sessions;
  std::vector<std::unique_ptr<ybthin_session>> write_sessions;
  std::atomic<size_t> read_rr{0};   // round-robin cursor for read-session selection
  std::atomic<size_t> write_rr{0};  // round-robin cursor for write-session selection
  std::vector<HostPort> hosts;
  MonoDelta timeout;
  int num_reactors = kDefaultNumReactors;

  // Keepalive. Deliberately std::thread, not yb::Thread: this .so is self-contained and must not
  // pull in YB's global thread registry / tracking machinery for a single background heartbeat.
  std::thread keepalive_thread;  // NOLINT(build/std_thread)
  std::mutex mu;
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
  size_t num_hash_key_columns = 0;
  size_t num_key_columns = 0;
  Schema schema;
  PartitionSchema partition_schema;
  std::vector<ColInfo> columns;  // owns the name strings referenced by ybthin_column
};

namespace {

// ---- allocation helpers (all shim-returned buffers use malloc/free) --------

char* DupCString(const std::string& s) {
  char* p = static_cast<char*>(malloc(s.size() + 1));
  if (p) {
    memcpy(p, s.data(), s.size());
    p[s.size()] = '\0';
  }
  return p;
}

uint8_t* DupBytes(const void* data, size_t len) {
  if (len == 0) {
    return nullptr;
  }
  uint8_t* p = static_cast<uint8_t*>(malloc(len));
  if (p) {
    memcpy(p, data, len);
  }
  return p;
}

// ---- status helpers -------------------------------------------------------

ybthin_status OkStatus() { return ybthin_status{YBTHIN_OK, nullptr}; }

ybthin_status MakeStatus(ybthin_status_code code, const std::string& msg) {
  return ybthin_status{code, code == YBTHIN_OK ? nullptr : DupCString(msg)};
}

ybthin_status_code ClassifyStatus(const Status& s) {
  if (s.ok()) return YBTHIN_OK;
  if (s.IsTimedOut() || s.IsNetworkError() || s.IsRemoteError()) return YBTHIN_NETWORK;
  if (s.IsTryAgain() || s.IsServiceUnavailable() || s.IsBusy()) return YBTHIN_TRY_AGAIN;
  if (s.IsInvalidArgument() || s.IsNotSupported()) return YBTHIN_INVALID;
  return YBTHIN_OTHER;
}

ybthin_status FromStatus(const Status& s) {
  if (s.ok()) return OkStatus();
  auto code = ClassifyStatus(s);
  const auto msg = s.ToString();
  // A dropped/expired session surfaces as an app error; steer the caller to reconnect.
  if (code == YBTHIN_OTHER || code == YBTHIN_INVALID) {
    if (msg.find("ession") != std::string::npos &&
        (msg.find("nknown") != std::string::npos || msg.find("xpired") != std::string::npos)) {
      code = YBTHIN_NETWORK;
    }
  }
  return MakeStatus(code, msg);
}

// ---- value / condition encoding -------------------------------------------

Status BindToQLValue(const ybthin_bind& b, yb::QLValuePB* out) {
  switch (b.tag) {
    case YBTHIN_BIND_NULL:
      return Status::OK();  // absent oneof == SQL NULL
    case YBTHIN_BIND_BOOL:
      out->set_bool_value(b.i != 0);
      return Status::OK();
    case YBTHIN_BIND_I16:
      out->set_int16_value(static_cast<int32_t>(b.i));
      return Status::OK();
    case YBTHIN_BIND_I32:
      out->set_int32_value(static_cast<int32_t>(b.i));
      return Status::OK();
    case YBTHIN_BIND_I64:
      out->set_int64_value(b.i);
      return Status::OK();
    case YBTHIN_BIND_TEXT:
      out->set_string_value(b.bytes, b.bytes_len);
      return Status::OK();
    case YBTHIN_BIND_BYTEA:
      out->set_binary_value(b.bytes, b.bytes_len);
      return Status::OK();
  }
  return STATUS_FORMAT(InvalidArgument, "bad bind tag $0", static_cast<int>(b.tag));
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

// Builds a single "<column> <op> <value>" comparison into `out`.
Status BuildComparison(const ybthin_cond& c, yb::PgsqlExpressionPB* out) {
  auto* cond = out->mutable_condition();
  cond->set_op(VERIFY_RESULT(MapCondOp(c.op)));
  cond->add_operands()->set_column_id(c.column_id);
  return BindToQLValue(c.value, cond->add_operands()->mutable_value());
}

// Sets hash_code + partition_key from already-populated partition_column_values so the tserver can
// route the op without a scan. No-op for a table with no hash columns.
template <class ReqPB>
Status SetPartitionRouting(const ybthin_table& table, ReqPB* req) {
  if (req->partition_column_values().empty()) {
    return Status::OK();
  }
  auto partition_key = VERIFY_RESULT(
      table.partition_schema.EncodePgsqlHash(req->partition_column_values()));
  req->set_hash_code(PartitionSchema::DecodeMultiColumnHashValue(partition_key));
  req->set_partition_key(std::move(partition_key));
  return Status::OK();
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
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    return STATUS_FORMAT(IOError, "cannot open $0", path);
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// Sends one Heartbeat. `create` true opens a new session (send pid); false keeps `session_id_`
// alive. On success the assigned/echoed session id is returned.
Result<uint64_t> DoHeartbeat(
    const tserver::PgClientServiceProxy& proxy, const MonoDelta& timeout, bool create,
    uint64_t session_id) {
  tserver::PgHeartbeatRequestPB req;
  if (create) {
    req.set_pid(static_cast<uint32_t>(getpid()));
  } else {
    req.set_session_id(session_id);
  }
  tserver::PgHeartbeatResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(timeout);
  RETURN_NOT_OK(proxy.Heartbeat(req, &resp, &controller));
  RETURN_NOT_OK(yb::ResponseStatus(resp));
  return resp.session_id();
}

// ---- session pool ---------------------------------------------------------

// (Re)opens a session on its connection: mints a fresh server session_id, resets the per-session
// serial to 0, and bumps the generation so any scan pinned to the old incarnation is invalidated.
// Caller must hold s->mu.
Status OpenSession(ybthin_client* client, ybthin_session* s) {
  auto& conn = *client->connections[s->conn_index];
  auto sid = DoHeartbeat(*conn.proxy, client->timeout, /* create= */ true, /* session_id= */ 0);
  RETURN_NOT_OK(sid);
  s->session_id = *sid;
  s->serial_no = 0;
  ++s->generation;
  s->open = true;
  return Status::OK();
}

// Lazily reopens a session that was torn down by a prior network failure. Caller holds s->mu.
Status EnsureSessionOpen(ybthin_client* client, ybthin_session* s) {
  return s->open ? Status::OK() : OpenSession(client, s);
}

// A network-class failure means the server may have dropped the session; drop it so the next use
// reopens with a fresh id (serials reset to 0).
void MaybeMarkSessionDead(ybthin_session* s, const ybthin_status& st) {
  if (st.code == YBTHIN_NETWORK) {
    std::lock_guard<std::mutex> lock(s->mu);
    s->open = false;
  }
}

size_t NextReadIndex(ybthin_client* client) {
  return client->read_rr.fetch_add(1, std::memory_order_relaxed) % client->read_sessions.size();
}

// Round-robins a write session; falls back to read sessions when there are none.
ybthin_session* NextWriteSession(ybthin_client* client) {
  auto& pool = client->write_sessions.empty() ? client->read_sessions : client->write_sessions;
  return pool[client->write_rr.fetch_add(1, std::memory_order_relaxed) % pool.size()].get();
}

// Wrapped paging state = [magic][version][u32 read-session index][u32 generation] + the server's
// paging state. The caller passes this back opaquely; on continuation the shim routes to the same
// session (generation-checked), which is where the server's read point for the scan lives.
constexpr uint8_t kPagingMagic = 0xB1;
constexpr uint8_t kPagingVersion = 1;
constexpr size_t kPagingHeaderLen = 2 + 4 + 4;

void PutU32LE(std::string* s, uint32_t v) {
  for (int i = 0; i < 4; ++i) s->push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}
uint32_t GetU32LE(const uint8_t* p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
  return v;
}

std::string WrapPagingState(uint32_t session_index, uint32_t generation, const std::string& srv) {
  std::string out;
  out.reserve(kPagingHeaderLen + srv.size());
  out.push_back(static_cast<char>(kPagingMagic));
  out.push_back(static_cast<char>(kPagingVersion));
  PutU32LE(&out, session_index);
  PutU32LE(&out, generation);
  out.append(srv);
  return out;
}

struct PinInfo {
  uint32_t session_index;
  uint32_t generation;
  Slice server_paging_state;
};
Result<PinInfo> UnwrapPagingState(const uint8_t* data, size_t len) {
  if (len < kPagingHeaderLen || data[0] != kPagingMagic || data[1] != kPagingVersion) {
    return STATUS(InvalidArgument, "malformed wrapped paging state");
  }
  return PinInfo{
      GetU32LE(data + 2), GetU32LE(data + 6),
      Slice(data + kPagingHeaderLen, len - kPagingHeaderLen)};
}

// Extracts the best human-readable message from a per-op response.
std::string PgsqlResponseMessage(const yb::PgsqlResponsePB& r) {
  if (r.error_status_size() > 0) {
    return yb::StatusFromPB(r.error_status(0)).ToString();
  }
  if (r.has_error_message()) {
    return r.error_message();
  }
  return "pgsql op failed";
}

// Maps a non-OK per-op response to a shim status.
ybthin_status PgsqlResponseError(const yb::PgsqlResponsePB& r) {
  switch (r.status()) {
    case yb::PgsqlResponsePB::PGSQL_STATUS_RESTART_REQUIRED_ERROR:
      return MakeStatus(YBTHIN_READ_RESTART, PgsqlResponseMessage(r));
    case yb::PgsqlResponsePB::PGSQL_STATUS_SCHEMA_VERSION_MISMATCH:
      return MakeStatus(YBTHIN_SCHEMA, PgsqlResponseMessage(r));
    default:
      if (r.error_status_size() > 0) {
        return FromStatus(yb::StatusFromPB(r.error_status(0)));
      }
      return MakeStatus(YBTHIN_OTHER, PgsqlResponseMessage(r));
  }
}

// ---- async call contexts --------------------------------------------------

struct ReadCall {
  ybthin_session* session;      // the one session this batch runs on
  uint32_t session_index;       // its index into client->read_sessions (for wrapping)
  uint32_t generation;          // session incarnation this batch ran at (for wrapping)
  bool has_continuation;        // any op is a continuation -> its pinned session must still match
  uint32_t pinned_generation;   // generation the continuation ops were pinned to
  tserver::PgPerformRequestPB req;
  tserver::PgPerformResponsePB resp;
  rpc::RpcController controller;
  ybthin_read_cb cb;
  void* ctx;
  uint64_t used_read_time_ht;
  // Per-op target column value types, in target (== response cell) order. The
  // row sidecar isn't self-describing, so decoding each op's response needs
  // these; the shim has them from the opened tables' schemas.
  std::vector<std::vector<ybthin_value_type>> op_target_types;
};

struct WriteCall {
  ybthin_session* session;
  tserver::PgPerformRequestPB req;
  tserver::PgPerformResponsePB resp;
  rpc::RpcController controller;
  ybthin_write_cb cb;
  void* ctx;
};

// Decodes the pg_doc_data row sidecar into a flat, row-major array of
// (n_rows * target_types.size()) cells using YugabyteDB's own PgWire/PgDocData
// readers (the single source of truth for the wire format). The cells and the
// TEXT/BYTEA byte payloads they point into are returned as ONE malloc'd block
// (the byte arena trails the cell array), so the caller frees it with a single
// free(*out_cells). `target_types` gives each target column's type in target
// (== cell) order, since the sidecar is not self-describing.
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

  for (int64_t r = 0; r < row_count; ++r) {
    for (size_t c = 0; c < n_cols; ++c) {
      const size_t idx = static_cast<size_t>(r) * n_cols + c;
      ybthin_cell& cell = cells[idx];
      if (VERIFY_RESULT(pggate::PgDocData::CheckedReadHeaderIsNull(&cursor))) {
        cell.tag = YBTHIN_BIND_NULL;
        continue;
      }
      switch (target_types[c]) {
        case YBTHIN_T_BOOL:
          cell.tag = YBTHIN_BIND_BOOL;
          cell.i = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<uint8_t>(&cursor));
          break;
        case YBTHIN_T_I16:
          cell.tag = YBTHIN_BIND_I16;
          cell.i = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<int16_t>(&cursor));
          break;
        case YBTHIN_T_I32:
          cell.tag = YBTHIN_BIND_I32;
          cell.i = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<int32_t>(&cursor));
          break;
        case YBTHIN_T_I64:
          cell.tag = YBTHIN_BIND_I64;
          cell.i = VERIFY_RESULT(pggate::PgWire::CheckedReadNumber<int64_t>(&cursor));
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

  // One block: the cell array followed by the byte arena the TEXT/BYTEA cells point into.
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

void FinishRead(ReadCall* raw) {
  std::unique_ptr<ReadCall> call(raw);

  Status rpc_status = call->controller.status();
  if (!rpc_status.ok()) {
    auto st = FromStatus(rpc_status);
    MaybeMarkSessionDead(call->session, st);
    call->cb(call->ctx, st, nullptr);
    return;
  }
  Status app_status = yb::ResponseStatus(call->resp);
  if (!app_status.ok()) {
    auto st = FromStatus(app_status);
    MaybeMarkSessionDead(call->session, st);
    call->cb(call->ctx, st, nullptr);
    return;
  }
  const size_t n_ops = call->op_target_types.size();
  if (static_cast<size_t>(call->resp.responses_size()) != n_ops) {
    call->cb(call->ctx, MakeStatus(YBTHIN_OTHER, "Perform op-response count mismatch"), nullptr);
    return;
  }
  // Batch-level status: a Perform fails/read-restarts as a unit, so any op error fails the whole
  // call with no partial results.
  for (size_t i = 0; i < n_ops; ++i) {
    const auto& op = call->resp.responses(static_cast<int>(i));
    if (op.status() != yb::PgsqlResponsePB::PGSQL_STATUS_OK) {
      call->cb(call->ctx, PgsqlResponseError(op), nullptr);
      return;
    }
  }

  auto* result = static_cast<ybthin_read_result*>(calloc(1, sizeof(ybthin_read_result)));
  result->n_ops = n_ops;
  result->used_read_time_ht = call->used_read_time_ht;
  result->results = static_cast<ybthin_read_op_result*>(
      calloc(n_ops ? n_ops : 1, sizeof(ybthin_read_op_result)));

  for (size_t i = 0; i < n_ops; ++i) {
    const auto& op = call->resp.responses(static_cast<int>(i));
    auto& op_result = result->results[i];
    op_result.n_cols = call->op_target_types[i].size();
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
          DecodeReadRows(sidecar->AsSlice(), call->op_target_types[i], &cells, &n_rows);
      if (!decoded.ok()) {
        ybthin_read_result_free(result);
        call->cb(call->ctx, FromStatus(decoded), nullptr);
        return;
      }
      op_result.cells = cells;
      op_result.n_rows = n_rows;
    }
    // A missing paging_state means this op's scan is exhausted; otherwise wrap the server's paging
    // state with the pinning header so a continuation returns to this batch's session.
    if (op.has_paging_state()) {
      std::string srv;
      op.paging_state().SerializeToString(&srv);
      std::string wrapped = WrapPagingState(call->session_index, call->generation, srv);
      op_result.paging_state = DupBytes(wrapped.data(), wrapped.size());
      op_result.paging_state_len = wrapped.size();
    }
  }
  call->cb(call->ctx, OkStatus(), result);
}

void FinishWrite(WriteCall* raw) {
  std::unique_ptr<WriteCall> call(raw);

  Status rpc_status = call->controller.status();
  if (!rpc_status.ok()) {
    auto st = FromStatus(rpc_status);
    MaybeMarkSessionDead(call->session, st);
    call->cb(call->ctx, st);
    return;
  }
  Status app_status = yb::ResponseStatus(call->resp);
  if (!app_status.ok()) {
    auto st = FromStatus(app_status);
    MaybeMarkSessionDead(call->session, st);
    call->cb(call->ctx, st);
    return;
  }
  // Every per-row op must have succeeded — a dropped row is an error, never a silent short write.
  for (const auto& op : call->resp.responses()) {
    if (op.status() != yb::PgsqlResponsePB::PGSQL_STATUS_OK) {
      call->cb(call->ctx, PgsqlResponseError(op));
      return;
    }
  }
  call->cb(call->ctx, OkStatus());
}

}  // namespace

// ===========================================================================
// C ABI
// ===========================================================================

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
  const uint32_t spc =
      (pool && pool->sessions_per_conn) ? pool->sessions_per_conn : kDefaultSessionsPerConn;
  if (read_n == 0) {
    return MakeStatus(YBTHIN_INVALID, "read_sessions must be >= 1");
  }
  const uint32_t num_conns = (read_n + write_n + spc - 1) / spc;

  auto client = std::make_unique<ybthin_client>();
  client->timeout =
      MonoDelta::FromMilliseconds(rpc_timeout_ms ? rpc_timeout_ms : kDefaultRpcTimeoutMs);
  client->num_reactors = num_reactors ? static_cast<int>(num_reactors) : kDefaultNumReactors;

  for (size_t i = 0; i < n_addrs; ++i) {
    auto hp = HostPort::FromString(tserver_addrs[i], kDefaultTserverRpcPort);
    if (hp.ok()) client->hosts.push_back(*hp);
  }
  if (client->hosts.empty()) {
    return MakeStatus(YBTHIN_INVALID, "no valid tserver addresses");
  }

  // Secure context (shared by every connection's messenger).
  if (tls && tls->ca_cert_path) {
    const bool mtls = tls->cert_path && tls->key_path;
    auto secure_context = std::make_unique<rpc::SecureContext>(
        rpc::RequireClientCertificate::kFalse, rpc::UseClientCertificate(mtls));
    if (mtls) {
      auto cert = ReadFile(tls->cert_path);
      if (!cert.ok()) return FromStatus(cert.status());
      auto key = ReadFile(tls->key_path);
      if (!key.ok()) return FromStatus(key.status());
      Status s = secure_context->UseCertificates(tls->ca_cert_path, Slice(*cert), Slice(*key));
      if (!s.ok()) return FromStatus(s);
    } else {
      Status s = secure_context->AddCertificateAuthorityFile(tls->ca_cert_path);
      if (!s.ok()) return FromStatus(s);
    }
    client->secure_context = std::move(secure_context);
  }

  auto shutdown_all = [&client] {
    for (auto& c : client->connections) {
      if (c->messenger) c->messenger->Shutdown();
    }
  };

  // One messenger (hence one socket) per connection, so connections spread across tserver nodes.
  for (uint32_t c = 0; c < num_conns; ++c) {
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
    conn->host = client->hosts[c % client->hosts.size()];
    conn->proxy =
        std::make_unique<tserver::PgClientServiceProxy>(conn->proxy_cache.get(), conn->host);
    client->connections.push_back(std::move(conn));
  }

  // Create and open sessions, packing `spc` per connection: global index g -> connection g / spc.
  uint32_t g = 0;
  auto add_session = [&](std::vector<std::unique_ptr<ybthin_session>>* dst) -> Status {
    auto s = std::make_unique<ybthin_session>();
    s->conn_index = g / spc;
    std::lock_guard<std::mutex> lock(s->mu);
    RETURN_NOT_OK(OpenSession(client.get(), s.get()));
    dst->push_back(std::move(s));
    ++g;
    return Status::OK();
  };
  for (uint32_t i = 0; i < read_n; ++i) {
    Status s = add_session(&client->read_sessions);
    if (!s.ok()) { shutdown_all(); return FromStatus(s); }
  }
  for (uint32_t i = 0; i < write_n; ++i) {
    Status s = add_session(&client->write_sessions);
    if (!s.ok()) { shutdown_all(); return FromStatus(s); }
  }

  // Keepalive: re-heartbeat every open session well inside the server's expiry window; a failed
  // heartbeat drops the session so the next use reopens it.
  ybthin_client* raw = client.get();
  raw->keepalive_thread = std::thread([raw] {  // NOLINT(build/std_thread)
    std::unique_lock<std::mutex> lock(raw->mu);
    while (!raw->stop) {
      if (raw->cv.wait_for(lock, std::chrono::milliseconds(kKeepaliveIntervalMs),
                           [raw] { return raw->stop; })) {
        break;
      }
      lock.unlock();
      const auto hb_timeout = std::min(raw->timeout, MonoDelta::FromSeconds(5));
      auto ping = [raw, hb_timeout](ybthin_session* s) {
        std::lock_guard<std::mutex> slock(s->mu);
        if (!s->open) return;
        auto r = DoHeartbeat(*raw->connections[s->conn_index]->proxy, hb_timeout,
                             /* create= */ false, s->session_id);
        if (!r.ok()) s->open = false;
      };
      for (auto& s : raw->read_sessions) ping(s.get());
      for (auto& s : raw->write_sessions) ping(s.get());
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
    std::lock_guard<std::mutex> lock(client->mu);
    client->stop = true;
  }
  client->cv.notify_all();
  if (client->keepalive_thread.joinable()) {
    client->keepalive_thread.join();
  }
  for (auto& conn : client->connections) {
    if (conn->messenger) conn->messenger->Shutdown();
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

  tserver::PgOpenTableRequestPB req;
  req.set_table_id(table->table_id);
  tserver::PgOpenTableResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(client->timeout);

  // OpenTable is session-less; any connection serves it.
  Status s = client->connections.front()->proxy->OpenTable(req, &resp, &controller);
  if (!s.ok()) return FromStatus(s);
  s = yb::ResponseStatus(resp);
  if (!s.ok()) return FromStatus(s);

  const auto& info = resp.info();
  table->schema_version = info.version();
  s = yb::SchemaFromPB(info.schema(), &table->schema);
  if (!s.ok()) return FromStatus(s);
  s = PartitionSchema::FromPB(info.partition_schema(), table->schema, &table->partition_schema);
  if (!s.ok()) return FromStatus(s);

  table->num_hash_key_columns = table->schema.num_hash_key_columns();
  table->num_key_columns = table->schema.num_key_columns();

  const size_t n = table->schema.num_columns();
  table->columns.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    const auto& col = table->schema.column(i);
    auto type = MapDataType(col.type()->main());
    if (!type.ok()) return FromStatus(type.status());
    ybthin_col_kind kind = i < table->num_hash_key_columns ? YBTHIN_COL_HASH
                           : i < table->num_key_columns    ? YBTHIN_COL_RANGE
                                                           : YBTHIN_COL_VALUE;
    table->columns.push_back(
        {col.name(), table->schema.column_id(i).rep(), kind, *type});
  }

  auto* out_cols = static_cast<ybthin_column*>(malloc(n * sizeof(ybthin_column)));
  for (size_t i = 0; i < n; ++i) {
    const auto& ci = table->columns[i];
    out_cols[i] = ybthin_column{ci.name.c_str(), ci.id, ci.kind, ci.type};
  }
  info_out->columns = out_cols;
  info_out->n_columns = n;

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
  call->used_read_time_ht = read_time_ht;

  // Select the batch's session. Continuation ops (paging_state_in set) return to the session that
  // issued them, so all continuations in a batch must pin the SAME session; a batch with no
  // continuations round-robins the read pool. Fresh ops ride whichever session is chosen.
  bool have_pinned = false;
  size_t session_index = 0;
  uint32_t pinned_generation = 0;
  std::vector<Slice> server_paging_states(n_ops);  // empty entry => fresh op
  for (size_t i = 0; i < n_ops; ++i) {
    const auto& op = ops[i];
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
    } else if (pin->session_index != session_index || pin->generation != pinned_generation) {
      cb(ctx, MakeStatus(
             YBTHIN_INVALID, "all continuation ops in a batch must share the paging session"),
         nullptr);
      return;
    }
    server_paging_states[i] = pin->server_paging_state;
  }
  if (!have_pinned) {
    session_index = NextReadIndex(client);
  }
  call->session = client->read_sessions[session_index].get();
  call->session_index = static_cast<uint32_t>(session_index);
  call->has_continuation = have_pinned;
  call->pinned_generation = pinned_generation;

  // Build one Perform with n_ops read requests, in caller order (results map back by index).
  auto& req = call->req;
  call->op_target_types.resize(n_ops);
  Status build = Status::OK();
  for (size_t i = 0; i < n_ops; ++i) {
    const ybthin_table* table = ops[i].table;
    const ybthin_read_spec* spec = &ops[i].spec;
    auto* read = req.add_ops()->mutable_read();
    read->set_client(yb::YQL_CLIENT_PGSQL);
    read->set_table_id(table->table_id);
    read->set_schema_version(table->schema_version);

    for (size_t h = 0; h < spec->n_hash && build.ok(); ++h) {
      build = BindToQLValue(
          spec->hash_values[h], read->add_partition_column_values()->mutable_value());
    }
    if (build.ok()) {
      build = SetPartitionRouting(*table, read);
    }
    if (build.ok() && spec->n_conds > 0) {
      if (spec->n_conds == 1) {
        build = BuildComparison(spec->conds[0], read->mutable_condition_expr());
      } else {
        auto* cond = read->mutable_condition_expr()->mutable_condition();
        cond->set_op(yb::QL_OP_AND);
        for (size_t c = 0; c < spec->n_conds && build.ok(); ++c) {
          build = BuildComparison(spec->conds[c], cond->add_operands());
        }
      }
    }
    if (!build.ok()) {
      cb(ctx, FromStatus(build), nullptr);
      return;
    }

    auto& types = call->op_target_types[i];
    types.reserve(spec->n_targets);
    for (size_t t = 0; t < spec->n_targets; ++t) {
      read->add_targets()->set_column_id(spec->target_ids[t]);
      read->add_col_refs()->set_column_id(spec->target_ids[t]);
      // Resolve the target column's type (from the opened schema) so the response sidecar can be
      // decoded; the sidecar carries values but no types.
      const ybthin_value_type* type = nullptr;
      for (const auto& col : table->columns) {
        if (col.id == spec->target_ids[t]) {
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
    if (!server_paging_states[i].empty()) {
      const Slice& ps = server_paging_states[i];
      if (!read->mutable_paging_state()->ParseFromArray(
              ps.data(), static_cast<int>(ps.size()))) {
        cb(ctx, MakeStatus(YBTHIN_INVALID, "could not parse paging_state_in"), nullptr);
        return;
      }
    }
  }

  // One snapshot for the whole batch: read_time_ht == 0 lets the server set a clamped read point
  // (relaxed read-after-commit); otherwise pin read == global_limit so all ops see one snapshot.
  auto* rto = req.mutable_options()->mutable_read_time_options();
  if (read_time_ht == 0) {
    rto->set_read_time_manipulation(tserver::ENSURE_READ_TIME_IS_SET);
    rto->set_clamp_uncertainty_window(true);
  } else {
    auto* rt = rto->mutable_read_time();
    rt->set_read_ht(read_time_ht);
    rt->set_global_limit_ht(read_time_ht);
  }

  // Bind the session identity and dispatch under the session lock: reopen the session if a prior
  // failure dropped it; a continuation whose session was reopened can no longer be served, so
  // report READ_RESTART. The Perform serial is allocated last so a failed build never leaves a gap.
  ReadCall* raw = call.release();
  auto* s = raw->session;
  ybthin_status early = OkStatus();
  bool dispatch = false;
  {
    std::lock_guard<std::mutex> lock(s->mu);
    Status open = EnsureSessionOpen(client, s);
    if (!open.ok()) {
      early = FromStatus(open);
    } else if (raw->has_continuation && s->generation != raw->pinned_generation) {
      early = MakeStatus(YBTHIN_READ_RESTART, "pinned read session was reopened");
    } else {
      raw->generation = s->generation;
      const uint64_t read_serial = s->read_time_serial++;
      rto->set_read_time_serial_no(read_serial);
      rto->set_read_time_serial_no_history_min(read_serial);
      for (auto& op : *raw->req.mutable_ops()) {
        op.mutable_read()->set_stmt_id(s->stmt_id++);
      }
      raw->req.set_session_id(s->session_id);
      raw->req.set_serial_no(s->serial_no++);
      dispatch = true;
    }
  }
  if (!dispatch) {
    std::unique_ptr<ReadCall> deleter(raw);
    cb(ctx, early, nullptr);
    return;
  }
  raw->controller.set_timeout(client->timeout);
  client->connections[s->conn_index]->proxy->PerformAsync(
      raw->req, &raw->resp, &raw->controller, [raw] { FinishRead(raw); });
}

void ybthin_read_result_free(ybthin_read_result* result) {
  if (!result) {
    return;
  }
  if (result->results) {
    for (size_t i = 0; i < result->n_ops; ++i) {
      // Each op's `cells` is a single block (cell array + trailing TEXT/BYTEA byte arena).
      free(result->results[i].cells);
      free(result->results[i].paging_state);
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
  call->session = NextWriteSession(client);

  auto& req = call->req;

  Status build = Status::OK();
  for (size_t r = 0; r < n_rows && build.ok(); ++r) {
    const auto& row = rows[r];
    const ybthin_table* table = row.table;
    auto* write = req.add_ops()->mutable_write();
    write->set_client(yb::YQL_CLIENT_PGSQL);
    write->set_stmt_type(yb::PgsqlWriteRequestPB::PGSQL_UPSERT);
    write->set_table_id(table->table_id);
    write->set_schema_version(table->schema_version);

    // Primary-key columns are supplied in schema order: hash columns first, then range columns.
    for (size_t i = 0; i < row.n_keys && build.ok(); ++i) {
      auto* expr = i < table->num_hash_key_columns ? write->add_partition_column_values()
                                                   : write->add_range_column_values();
      build = BindToQLValue(row.key_values[i], expr->mutable_value());
    }
    if (build.ok()) {
      build = SetPartitionRouting(*table, write);
    }
    for (size_t i = 0; i < row.n_values && build.ok(); ++i) {
      auto* cv = write->add_column_values();
      cv->set_column_id(row.value_ids[i]);
      build = BindToQLValue(row.values[i], cv->mutable_expr()->mutable_value());
    }
  }
  if (!build.ok()) {
    cb(ctx, FromStatus(build));
    return;
  }

  // Bind the session identity and dispatch under the session lock. A non-transactional write
  // carries a read-time serial (+ history min) and lets the storage layer pick the read time for
  // conflict resolution (pg_txn_manager.cc SetupReadTimeOptions). The Perform serial is allocated
  // last so a failed build never leaves a gap in the session's contiguous serial sequence.
  WriteCall* raw = call.release();
  auto* s = raw->session;
  ybthin_status early = OkStatus();
  bool dispatch = false;
  {
    std::lock_guard<std::mutex> lock(s->mu);
    Status open = EnsureSessionOpen(client, s);
    if (!open.ok()) {
      early = FromStatus(open);
    } else {
      for (auto& op : *raw->req.mutable_ops()) {
        op.mutable_write()->set_stmt_id(s->stmt_id++);
      }
      auto* rto = raw->req.mutable_options()->mutable_read_time_options();
      const uint64_t serial = s->read_time_serial++;
      rto->set_read_time_serial_no(serial);
      rto->set_read_time_serial_no_history_min(serial);
      raw->req.set_session_id(s->session_id);
      raw->req.set_serial_no(s->serial_no++);
      dispatch = true;
    }
  }
  if (!dispatch) {
    std::unique_ptr<WriteCall> deleter(raw);
    cb(ctx, early);
    return;
  }
  raw->controller.set_timeout(client->timeout);
  client->connections[s->conn_index]->proxy->PerformAsync(
      raw->req, &raw->resp, &raw->controller, [raw] { FinishWrite(raw); });
}

void ybthin_string_free(char* s) { free(s); }

}  // extern "C"
