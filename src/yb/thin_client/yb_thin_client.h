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

// yb_thin_client.h -- C ABI for a thin, Perform-based YugabyteDB tserver client.
//
// This header is the ONLY contract between the (foreign) caller and the C++ implementation
// (yb_thin_client.cc, built against the YugabyteDB tree). The client speaks
// yb.tserver.ThinClientService.Perform directly to a tserver RPC endpoint and lets the tserver
// route each op to the right tablet -- there is deliberately NO metacache, batcher or tablet
// invoker on the caller's side.
//
// VERSIONING: this library is built OUTSIDE the YugabyteDB tree and upgraded BEFORE the tserver, so
// a given build must keep working against an OLDER one. On the wire, only rely on request fields,
// RPCs and semantics the OLDEST supported tserver understands, and tolerate responses missing
// fields a newer server would set. The ABI below is likewise stable -- changes must be ADDITIVE
// (new functions; fields appended to the END of structs; enum values appended at the END). Never
// renumber an enum value, reorder or remove a struct field, or change a signature in place.
//
// Ownership: every out-pointer the library fills is owned by it and freed with the matching _free
// or _destroy call below. Input arrays and strings are borrowed for the duration of the call only.
// All strings are UTF-8, NUL-terminated. The header is C (extern "C"): it must compile under a
// plain C compiler and expose no C++ types.

#ifndef YB_THIN_CLIENT_YB_THIN_CLIENT_H
#define YB_THIN_CLIENT_YB_THIN_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  YBTHIN_OK = 0,
  YBTHIN_INVALID = 1,      // bad arguments / misuse -- never retry
  YBTHIN_NETWORK = 2,      // transport loss, timeout, or lost session
  YBTHIN_TRY_AGAIN = 3,    // transient server state (leader not ready...)
  YBTHIN_READ_RESTART = 4, // restart_read_time: caller restarts the scan
  YBTHIN_SCHEMA = 5,       // schema-version mismatch -- reopen the table
  YBTHIN_OTHER = 6,        // anything else -- fatal
  YBTHIN_FENCED = 7,       // ignore_after_hybrid_time had passed; the write did NOT take effect
} ybthin_status_code;

// `message` is owned (free with ybthin_string_free) and NULL when code == YBTHIN_OK.
typedef struct {
  ybthin_status_code code;
  char* message;
} ybthin_status;

typedef struct ybthin_client ybthin_client;
typedef struct ybthin_table ybthin_table;

typedef struct {
  const char* ca_cert_path; // NULL => plaintext (no TLS)
  const char* cert_path;    // client cert; NULL if not using mTLS
  const char* key_path;     // client key;  NULL if not using mTLS
} ybthin_tls_opts;

// A session applies one Perform at a time, so concurrency comes from having many sessions;
// connections spread them across tserver nodes (behind a ClusterIP VIP, each new connection may
// land on a different one). `sessions_per_conn` sessions are packed per
// connection, so ceil((read_sessions + write_sessions) / sessions_per_conn) are opened. Reads and
// upserts each round-robin their own pool. A 0 field (or NULL opts) takes the default.
typedef struct {
  uint32_t read_sessions;     // 0 => default (4)
  uint32_t write_sessions;    // 0 => default (1); 0 sessions => upserts use the read pool
  uint32_t sessions_per_conn; // 0 => default (4)
} ybthin_pool_opts;

// Connect to one or more tserver RPC endpoints ("host:port", default port 9100), open a pool of
// ThinClientService sessions and start their keepalive. No master addresses are needed -- routing
// is server-side.
ybthin_status ybthin_client_create(const char* const* tserver_addrs,
                                   size_t n_addrs,
                                   const ybthin_tls_opts* tls,   // nullable
                                   const ybthin_pool_opts* pool, // nullable
                                   uint32_t rpc_timeout_ms,
                                   uint32_t num_reactors, // 0 => default
                                   ybthin_client** out);

void ybthin_client_destroy(ybthin_client*);

typedef enum {
  YBTHIN_COL_HASH = 0,
  YBTHIN_COL_RANGE = 1,
  YBTHIN_COL_VALUE = 2,
} ybthin_col_kind;

typedef enum {
  YBTHIN_T_BOOL = 0,
  YBTHIN_T_I16 = 1,
  YBTHIN_T_I32 = 2,
  YBTHIN_T_I64 = 3,
  YBTHIN_T_TEXT = 4,
  YBTHIN_T_BYTEA = 5,
} ybthin_value_type;

typedef struct {
  const char* name; // owned by the table handle (valid until close)
  int32_t id;       // DocDB column id
  ybthin_col_kind kind;
  ybthin_value_type type;
} ybthin_column;

// Filled by ybthin_table_open. `columns` is owned by the shim (free with ybthin_columns_free) and
// kept in schema order -- hash, then range, then value -- so key values can be bound positionally.
typedef struct {
  ybthin_column* columns;
  size_t n_columns;
} ybthin_table_info;

// Computes the table's pgsql table id and fetches its schema. Fails fast if the tserver or table is
// unreachable, which makes it usable as a startup health check.
ybthin_status ybthin_table_open(ybthin_client*, uint32_t db_oid,
                                uint32_t table_oid, ybthin_table** out,
                                ybthin_table_info* info_out);

void ybthin_table_close(ybthin_table*);
void ybthin_columns_free(ybthin_column*, size_t n);

typedef enum {
  YBTHIN_BIND_NULL = 0,
  YBTHIN_BIND_BOOL = 1,
  YBTHIN_BIND_I16 = 2,
  YBTHIN_BIND_I32 = 3,
  YBTHIN_BIND_I64 = 4,
  YBTHIN_BIND_TEXT = 5,
  YBTHIN_BIND_BYTEA = 6,
} ybthin_bind_tag;

// For BOOL/I16/I32/I64 read `int_value`; for TEXT/BYTEA read (`bytes`, `bytes_len`).
typedef struct {
  ybthin_bind_tag tag;
  int64_t int_value;
  const uint8_t* bytes;
  size_t bytes_len;
} ybthin_bind;

typedef enum {
  YBTHIN_EQ = 0,
  YBTHIN_LE = 1,
  YBTHIN_GE = 2,
  YBTHIN_LT = 3,
  YBTHIN_GT = 4,
} ybthin_cond_op;

typedef struct {
  int32_t column_id;
  ybthin_cond_op op;
  ybthin_bind value;
} ybthin_cond;

typedef struct {
  const ybthin_bind* hash_values; // hash cols in schema order
  size_t n_hash;
  const ybthin_cond* conds; // AND-combined
  size_t n_conds;
  const int32_t* target_ids; // column ids to return, in order
  size_t n_targets;
  uint64_t limit;      // rows per page; 0 => server default
  int is_forward_scan; // bool
  // Range key cols in schema order, for a RANGE-sharded table: a full key targets one row, a prefix
  // that prefix's range, NULL/0 the whole table. Mixing these with hash_values is YBTHIN_INVALID.
  const ybthin_bind* range_values;
  size_t n_range;
} ybthin_read_spec;

// A decoded cell of a read result, mirroring ybthin_bind. `bytes` points into result-owned storage,
// valid until ybthin_read_result_free.
typedef struct {
  ybthin_bind_tag tag;
  int64_t int_value;
  const uint8_t* bytes;
  size_t bytes_len;
} ybthin_cell;

// One read op in a batch; a batch may mix tables. `paging_state_in`==NULL (len 0) starts this op's
// scan, a prior page's paging_state continues it. The read snapshot is a batch property
// (ybthin_read_async's read_time_ht), NOT part of the spec.
typedef struct {
  ybthin_table* table;
  ybthin_read_spec spec;
  const uint8_t* paging_state_in;
  size_t paging_state_in_len;
} ybthin_read_op;

// Per-op slice of a batch result. Cells are row-major in target order: row r, column c is
// `cells[r * n_cols + c]`, with `n_cols == the op's spec.n_targets`. `paging_state` is NULL/0 when
// this op's scan is exhausted; otherwise pass it back verbatim as a later op's `paging_state_in`.
typedef struct {
  ybthin_cell* cells;
  size_t n_rows;
  size_t n_cols;
  uint8_t* paging_state;
  size_t paging_state_len;
} ybthin_read_op_result;

// `results[i]` corresponds to the i-th op passed to ybthin_read_async. `used_read_time_ht` is the
// hybrid time the WHOLE batch ran at (one snapshot per Perform; 0 if the server reported none).
// Owned by the shim; free once with ybthin_read_result_free.
typedef struct {
  ybthin_read_op_result* results;
  size_t n_ops;
  uint64_t used_read_time_ht;
} ybthin_read_result;

void ybthin_read_result_free(ybthin_read_result*);

// One PGSQL_UPSERT row: its table, PK columns in schema order, plus non-key cells as parallel
// (id, value) arrays. Each row names its table, so a batch may span tables and still ride ONE
// Perform.
typedef struct {
  ybthin_table* table;
  const ybthin_bind* key_values;
  size_t n_keys;
  const int32_t* value_ids;
  const ybthin_bind* values;
  size_t n_values;
  // Fences this row against a lease the caller holds: the leader rejects the write with
  // YBTHIN_FENCED if this hybrid time has already passed by the time the op is assigned its own.
  // 0 means no fence. Judged per tablet, so a batch straddling the boundary can be partly applied.
  uint64_t ignore_after_hybrid_time;
} ybthin_upsert_row;

// Completion callbacks may run on a YB reactor thread, so they MUST be cheap and non-blocking
// (signal a channel and return). `status`/`result` are owned by the callback: copy what you need,
// then free them (ybthin_string_free / ybthin_read_result_free) before returning.
typedef void (*ybthin_read_cb)(void* ctx, ybthin_status status,
                               ybthin_read_result* result);
typedef void (*ybthin_write_cb)(void* ctx, ybthin_status status);

// Run a batch of read ops as the ops of ONE Perform: one RPC, one read session, one snapshot.
// `read_time_ht` pins the batch snapshot (0 => the server picks one); pass a prior batch's
// `used_read_time_ht` only to force a NEW batch onto an existing snapshot. An op with
// `paging_state_in` set continues that op's scan and stays on the scan's original snapshot
// automatically -- the time the first page was served at travels inside `paging_state`, so the
// server keeps no read state -- and paging with `read_time_ht` left 0 throughout is still
// consistent: no rows are dropped mid-scan.
// Continuation ops in one batch must all share the paging session that issued
// them. Status is batch-level: any op failure fails the whole call with no partial results; on
// YBTHIN_READ_RESTART re-issue the scan from a fresh page.
void ybthin_read_async(ybthin_client*, const ybthin_read_op* ops, size_t n_ops,
                       uint64_t read_time_ht, ybthin_read_cb cb, void* ctx);

// Apply N upserts as the ops of ONE Perform, on one write session. Each row names its own table, so
// the batch may span tables. Every per-row response status is verified; any failure surfaces via
// `status`.
void ybthin_upsert_batch_async(ybthin_client*, const ybthin_upsert_row* rows,
                               size_t n_rows, ybthin_write_cb cb, void* ctx);

void ybthin_string_free(char*);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // YB_THIN_CLIENT_YB_THIN_CLIENT_H
