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

/* yb_thin_client.h — C ABI for a thin, Perform-based YugabyteDB tserver client.
 *
 * This header is the ONLY contract between the (foreign) caller and the C++
 * implementation (`yb_thin_client.cc`, built against the YugabyteDB tree). The
 * client speaks `yb.tserver.PgClientService.Perform` directly to a tserver
 * RPC endpoint and lets the tserver route each op to the right tablet —
 * there is deliberately NO client-side metacache, batcher, or tablet
 * invoker on the caller's side.
 *
 * VERSIONING / BACKWARD COMPATIBILITY: this library is built, shipped and
 * consumed OUTSIDE the YugabyteDB build tree, and it is upgraded BEFORE the
 * tserver — so a given build of it must keep working against an OLDER tserver
 * than the one it was compiled against. Keep its on-the-wire usage
 * BACKWARD-COMPATIBLE with older servers:
 *   - Only send Perform requests / set protobuf fields / rely on RPCs and
 *     semantics that the OLDEST supported tserver already understands. Never
 *     require a request field, op, or behavior that only a newer tserver added
 *     — an older server will ignore or reject it.
 *   - Tolerate responses from an older server that lack fields a newer server
 *     would set; never depend on such a field being present.
 * The C ABI below is likewise a stable contract: make only ADDITIVE changes
 * (new functions; new fields appended to the END of structs; new enum values
 * appended at the END). Never renumber/repurpose an existing enum value,
 * reorder or remove a struct field, or change a function signature in place.
 *
 * Ownership: every out-pointer the library fills is owned by it and freed
 * with the matching `*_free`/`*_destroy` below. Input arrays and strings are
 * borrowed for the duration of the call only. All strings are UTF-8,
 * NUL-terminated. The header is C (extern "C"); it must compile under a plain
 * C compiler and expose no C++ types.
 */
#ifndef YB_THIN_CLIENT_YB_THIN_CLIENT_H
#define YB_THIN_CLIENT_YB_THIN_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- status --------------------------------------------------------- */

typedef enum {
  YBTHIN_OK = 0,
  YBTHIN_INVALID = 1,      /* bad arguments / misuse — never retry        */
  YBTHIN_NETWORK = 2,      /* transport loss, timeout, or lost session    */
  YBTHIN_TRY_AGAIN = 3,    /* transient server state (leader not ready…)  */
  YBTHIN_READ_RESTART = 4, /* restart_read_time: caller restarts the scan */
  YBTHIN_SCHEMA = 5,       /* schema-version mismatch — reopen the table  */
  YBTHIN_OTHER = 6,        /* anything else — fatal                       */
} ybthin_status_code;

/* Returned by fallible calls. `message` is owned (free with
 * ybthin_string_free) and NULL when code == YBTHIN_OK. */
typedef struct {
  ybthin_status_code code;
  char* message;
} ybthin_status;

/* ---- handles -------------------------------------------------------- */

typedef struct ybthin_client ybthin_client; /* one per process component  */
typedef struct ybthin_table ybthin_table;   /* an opened table handle      */

/* ---- client lifecycle ----------------------------------------------- */

typedef struct {
  const char* ca_cert_path; /* NULL => plaintext (no TLS)                 */
  const char* cert_path;    /* client cert; NULL if not using mTLS        */
  const char* key_path;     /* client key;  NULL if not using mTLS        */
} ybthin_tls_opts;

/* Session/connection pool sizing. A single PgClientService session serializes
 * its Performs by serial number server-side, so concurrency comes from having
 * many sessions; connections spread those sessions across tserver nodes (behind
 * a ClusterIP VIP, each new connection may land on a different tserver).
 * `sessions_per_conn` sessions are packed onto each connection; the client opens
 * ceil((read_sessions + write_sessions) / sessions_per_conn) connections.
 * Reads round-robin the read sessions; upserts round-robin the write sessions
 * (falling back to read sessions when write_sessions == 0). A field of 0 (or a
 * NULL ybthin_pool_opts) selects the default for that field. */
typedef struct {
  uint32_t read_sessions;      /* 0 => default (4)                          */
  uint32_t write_sessions;     /* 0 => default (1)                          */
  uint32_t sessions_per_conn;  /* 0 => default (4)                          */
} ybthin_pool_opts;

/* Connect to one or more tserver RPC endpoints ("host:port", default port
 * 9100). Opens a pool of PgClientService sessions (Heartbeat) across one or
 * more connections and starts their keepalive. No master addresses are needed —
 * routing is server-side. */
ybthin_status ybthin_client_create(const char* const* tserver_addrs,
                                   size_t n_addrs,
                                   const ybthin_tls_opts* tls,   /* nullable */
                                   const ybthin_pool_opts* pool, /* nullable */
                                   uint32_t rpc_timeout_ms,
                                   uint32_t num_reactors, /* 0 => default   */
                                   ybthin_client** out);

void ybthin_client_destroy(ybthin_client*);

/* ---- table open + schema -------------------------------------------- */

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
  const char* name; /* owned by the table handle (valid until close)      */
  int32_t id;       /* DocDB column id                                    */
  ybthin_col_kind kind;
  ybthin_value_type type;
} ybthin_column;

/* Filled by ybthin_table_open. `columns` is owned by the shim; free with
 * ybthin_columns_free. Kept in schema order (hash, then range, then
 * value), so the caller can bind hash/key values positionally. */
typedef struct {
  ybthin_column* columns;
  size_t n_columns;
} ybthin_table_info;

/* Resolve a table by (db_oid, table_oid): computes its pgsql table id and
 * fetches its schema. Fails fast if the tserver/table is unreachable —
 * used as the startup health check. */
ybthin_status ybthin_table_open(ybthin_client*, uint32_t db_oid,
                                uint32_t table_oid, ybthin_table** out,
                                ybthin_table_info* info_out);

void ybthin_table_close(ybthin_table*);
void ybthin_columns_free(ybthin_column*, size_t n);

/* ---- values, conditions, read spec ---------------------------------- */

typedef enum {
  YBTHIN_BIND_NULL = 0,
  YBTHIN_BIND_BOOL = 1,
  YBTHIN_BIND_I16 = 2,
  YBTHIN_BIND_I32 = 3,
  YBTHIN_BIND_I64 = 4,
  YBTHIN_BIND_TEXT = 5,
  YBTHIN_BIND_BYTEA = 6,
} ybthin_bind_tag;

/* A bound value. For BOOL/I16/I32/I64 read `i`; for TEXT/BYTEA read
 * (`bytes`, `bytes_len`). The shim encodes each per its column's DocDB
 * type (raw big-endian ints, raw bytes for text/binary). */
typedef struct {
  ybthin_bind_tag tag;
  int64_t i;
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
  const ybthin_bind* hash_values; /* hash cols in schema order            */
  size_t n_hash;
  const ybthin_cond* conds; /* range/lsn bounds (AND-combined)            */
  size_t n_conds;
  const int32_t* target_ids; /* column ids to return, in order            */
  size_t n_targets;
  uint64_t limit;      /* rows per page; 0 => server default              */
  int is_forward_scan; /* bool: PK stores lsn DESC, so forward == desc    */
} ybthin_read_spec;

/* A decoded cell of a read result. NULL is a tag (tag == YBTHIN_BIND_NULL);
 * BOOL/I16/I32/I64 read `i`; TEXT/BYTEA read (`bytes`, `bytes_len`). `bytes`
 * point into result-owned storage, valid until ybthin_read_result_free.
 * Mirrors ybthin_bind (the write side) so the value model is symmetric. */
typedef struct {
  ybthin_bind_tag tag;
  int64_t i;
  const uint8_t* bytes;
  size_t bytes_len;
} ybthin_cell;

/* One read op in a batch. Each op names its own table (a batch may mix
 * tables). `paging_state_in`==NULL (len 0) starts this op's scan; a prior
 * page's paging_state continues it. Note: the read snapshot is a batch
 * property (ybthin_read_async's read_time_ht), NOT part of the spec. */
typedef struct {
  ybthin_table* table;
  ybthin_read_spec spec;
  const uint8_t* paging_state_in;
  size_t paging_state_in_len;
} ybthin_read_op;

/* Per-op slice of a batch result, decoded by the shim (using YugabyteDB's own
 * pg_doc_data codec) into a row-major array of typed cells: the cell at row r,
 * column c is `cells[r * n_cols + c]`, and `n_cols == the op's spec.n_targets`
 * (cells follow target order). `paging_state` is NULL/0 when this op's scan is
 * exhausted; otherwise pass it back verbatim as a later op's `paging_state_in`
 * to continue. */
typedef struct {
  ybthin_cell* cells;
  size_t n_rows;
  size_t n_cols;
  uint8_t* paging_state;
  size_t paging_state_len;
} ybthin_read_op_result;

/* Whole-batch result: `results[i]` corresponds to the i-th op passed to
 * ybthin_read_async. `used_read_time_ht` is the hybrid time the WHOLE batch
 * ran at (one snapshot per Perform; 0 if the server did not report one).
 * Owned by the shim; free once with ybthin_read_result_free. */
typedef struct {
  ybthin_read_op_result* results;
  size_t n_ops;
  uint64_t used_read_time_ht;
} ybthin_read_result;

void ybthin_read_result_free(ybthin_read_result*);

/* ---- upsert (write) rows -------------------------------------------- */

/* One PGSQL_UPSERT row: its table, PK columns (schema order), plus non-key
 * cells as parallel (id, value) arrays. Each row names its table, so a write
 * batch may span tables (all rows still ride ONE Perform). */
typedef struct {
  ybthin_table* table;
  const ybthin_bind* key_values;
  size_t n_keys;
  const int32_t* value_ids;
  const ybthin_bind* values;
  size_t n_values;
} ybthin_upsert_row;

/* ---- async execution ------------------------------------------------ */

/* Completion callbacks. They may run on a YB reactor thread, so they MUST
 * be cheap and non-blocking (signal a channel and return). `ctx` is the
 * opaque pointer handed to the call. `status`/`result` are owned by the
 * callback: copy what you need, then free them (ybthin_string_free /
 * ybthin_read_result_free) before returning. */
typedef void (*ybthin_read_cb)(void* ctx, ybthin_status status,
                               ybthin_read_result* result);
typedef void (*ybthin_write_cb)(void* ctx, ybthin_status status);

/* Run a batch of read ops as the ops of ONE Perform: one RPC, one read
 * session, one snapshot. `results[i]` in the delivered ybthin_read_result
 * corresponds to `ops[i]`. `read_time_ht` pins the batch snapshot (0 => the
 * server picks a clamped read point); pass a prior batch's `used_read_time_ht`
 * to continue its scans under the same snapshot. An op with `paging_state_in`
 * set continues that op's scan; mixed fresh/continuation batches are legal, but
 * all continuation ops must share the paging session that issued them.
 * Status is batch-level: any op failure fails the whole call (no partial
 * results) — on YBTHIN_READ_RESTART the caller re-issues from fresh pages. */
void ybthin_read_async(ybthin_client*, const ybthin_read_op* ops, size_t n_ops,
                       uint64_t read_time_ht, ybthin_read_cb cb, void* ctx);

/* Apply N upserts as the ops of ONE Perform (one write batch on one write
 * session). Each row names its own table, so the batch may span tables. The
 * shim verifies every op's per-row response status; any failure surfaces via
 * `status`. */
void ybthin_upsert_batch_async(ybthin_client*, const ybthin_upsert_row* rows,
                               size_t n_rows, ybthin_write_cb cb, void* ctx);

/* ---- misc ----------------------------------------------------------- */

void ybthin_string_free(char*);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif  // YB_THIN_CLIENT_YB_THIN_CLIENT_H
