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

// This module contains C definitions for all YugaByte structures that are used to exhange data
// and metadata between Postgres and YBClient libraries.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "yb/yql/pggate/pg_metrics_list.h"

#ifdef __cplusplus

#define YB_DEFINE_HANDLE_TYPE(name) \
    namespace yb { \
    namespace pggate { \
    class name; \
    } \
    } \
    typedef class yb::pggate::name *Ybc##name;

#define YB_PGGATE_IDENTIFIER(name) yb::pggate::name

#else
#define YB_DEFINE_HANDLE_TYPE(name) typedef struct name *Ybc##name;
#define YB_PGGATE_IDENTIFIER(name) name
#endif  // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Handle to a session. Postgres should create one YbcPgSession per client connection.
YB_DEFINE_HANDLE_TYPE(PgSession)

// Handle to a statement.
YB_DEFINE_HANDLE_TYPE(PgStatement)

// Handle to an expression.
YB_DEFINE_HANDLE_TYPE(PgExpr);

// Handle to a postgres function
YB_DEFINE_HANDLE_TYPE(PgFunction);

// Handle to a table description
YB_DEFINE_HANDLE_TYPE(PgTableDesc);

// Handle to a memory context.
YB_DEFINE_HANDLE_TYPE(PgMemctx);

// Represents STATUS_* definitions from src/postgres/src/include/c.h.
#define YBC_STATUS_OK     (0)
#define YBC_STATUS_ERROR  (-1)

//--------------------------------------------------------------------------------------------------
// Other definitions are the same between C++ and C.
//--------------------------------------------------------------------------------------------------
// Use YugaByte (YQL) datatype numeric representation for now, as provided in common.proto.
// TODO(neil) This should be change to "PgType *" and convert Postgres's TypeName struct to our
// class PgType or QLType.
typedef enum {
  YB_YQL_DATA_TYPE_NOT_SUPPORTED = -1,
  YB_YQL_DATA_TYPE_UNKNOWN_DATA = 999,
  YB_YQL_DATA_TYPE_NULL_VALUE_TYPE = 0,
  YB_YQL_DATA_TYPE_INT8 = 1,
  YB_YQL_DATA_TYPE_INT16 = 2,
  YB_YQL_DATA_TYPE_INT32 = 3,
  YB_YQL_DATA_TYPE_INT64 = 4,
  YB_YQL_DATA_TYPE_STRING = 5,
  YB_YQL_DATA_TYPE_BOOL = 6,
  YB_YQL_DATA_TYPE_FLOAT = 7,
  YB_YQL_DATA_TYPE_DOUBLE = 8,
  YB_YQL_DATA_TYPE_BINARY = 9,
  YB_YQL_DATA_TYPE_TIMESTAMP = 10,
  YB_YQL_DATA_TYPE_DECIMAL = 11,
  YB_YQL_DATA_TYPE_VARINT = 12,
  YB_YQL_DATA_TYPE_INET = 13,
  YB_YQL_DATA_TYPE_LIST = 14,
  YB_YQL_DATA_TYPE_MAP = 15,
  YB_YQL_DATA_TYPE_SET = 16,
  YB_YQL_DATA_TYPE_UUID = 17,
  YB_YQL_DATA_TYPE_TIMEUUID = 18,
  YB_YQL_DATA_TYPE_TUPLE = 19,
  YB_YQL_DATA_TYPE_TYPEARGS = 20,
  YB_YQL_DATA_TYPE_USER_DEFINED_TYPE = 21,
  YB_YQL_DATA_TYPE_FROZEN = 22,
  YB_YQL_DATA_TYPE_DATE = 23,
  YB_YQL_DATA_TYPE_TIME = 24,
  YB_YQL_DATA_TYPE_JSONB = 25,
  YB_YQL_DATA_TYPE_UINT8 = 100,
  YB_YQL_DATA_TYPE_UINT16 = 101,
  YB_YQL_DATA_TYPE_UINT32 = 102,
  YB_YQL_DATA_TYPE_UINT64 = 103,
  YB_YQL_DATA_TYPE_GIN_NULL = 104,
  YB_YQL_DATA_TYPE_VECTOR = 105,
  YB_YQL_DATA_TYPE_BSON = 106,
} YbcPgDataType;

// Datatypes that are internally designated to be unsupported.
// (See similar QL_UNSUPPORTED_TYPES_IN_SWITCH.)
#define YB_PG_UNSUPPORTED_TYPES_IN_SWITCH \
  case YB_YQL_DATA_TYPE_NOT_SUPPORTED: \
  case YB_YQL_DATA_TYPE_UNKNOWN_DATA

// Datatypes that are not used in YSQL.
// (See similar QL_INVALID_TYPES_IN_SWITCH.)
#define YB_PG_INVALID_TYPES_IN_SWITCH \
  case YB_YQL_DATA_TYPE_NULL_VALUE_TYPE: \
  case YB_YQL_DATA_TYPE_VARINT: \
  case YB_YQL_DATA_TYPE_INET: \
  case YB_YQL_DATA_TYPE_LIST: \
  case YB_YQL_DATA_TYPE_MAP: \
  case YB_YQL_DATA_TYPE_SET: \
  case YB_YQL_DATA_TYPE_UUID: \
  case YB_YQL_DATA_TYPE_TIMEUUID: \
  case YB_YQL_DATA_TYPE_TUPLE: \
  case YB_YQL_DATA_TYPE_TYPEARGS: \
  case YB_YQL_DATA_TYPE_USER_DEFINED_TYPE: \
  case YB_YQL_DATA_TYPE_FROZEN: \
  case YB_YQL_DATA_TYPE_DATE: \
  case YB_YQL_DATA_TYPE_TIME: \
  case YB_YQL_DATA_TYPE_JSONB: \
  case YB_YQL_DATA_TYPE_UINT8: \
  case YB_YQL_DATA_TYPE_UINT16

// Datatype representation:
// Definition of a datatype is divided into two different sections.
// - YbcPgTypeEntity is used to keep static information of a datatype.
// - YbcPgTypeAttrs is used to keep customizable information of a datatype.
//
// Example:
//   For type CHAR(20), its associated YugaByte internal type (YB_YQL_DATA_TYPE_STRING) is
//   static while its typemod (size 20) can be customized for each usage.
typedef struct {
  // Currently, we only need typmod, but we might need more datatype information in the future.
  // For example, array dimensions might be needed.
  int32_t typmod;
} YbcPgTypeAttrs;

// Datatype conversion functions.
typedef void (*YbcPgDatumToData)(uint64_t datum, void *ybdata, int64_t *bytes);
typedef uint64_t (*YbcPgDatumFromData)(const void *ybdata, int64_t bytes,
                                       const YbcPgTypeAttrs *type_attrs);
typedef struct {
  // Postgres type OID.
  int type_oid;

  // YugaByte storage (DocDB) type.
  YbcPgDataType yb_type;

  // Allow to be used for primary key.
  bool allow_for_primary_key;

  // Datum in-memory fixed size.
  // - Size of in-memory representation for a type. Usually it's sizeof(a_struct).
  //   Example: BIGINT in-memory size === sizeof(int64)
  //            POINT in-memory size === sizeof(struct Point)
  // - Set to (-1) for types of variable in-memory size - VARSIZE_ANY should be used.
  int64_t datum_fixed_size;

  // Whether we could use cast to convert value to datum.
  bool direct_datum;

  // Converting Postgres datum to YugaByte expression.
  YbcPgDatumToData datum_to_yb;

  // Converting YugaByte values to Postgres in-memory-formatted datum.
  YbcPgDatumFromData yb_to_datum;
} YbcPgTypeEntity;

typedef struct {
  const YbcPgTypeEntity *data;
  uint32_t count;
} YbcPgTypeEntities;

// Kind of a datum.
// In addition to datatype, a "datum" is also specified by "kind".
// - Standard value.
// - MIN limit value, which can be infinite, represents an absolute mininum value of a datatype.
// - MAX limit value, which can be infinite, represents an absolute maximum value of a datatype.
//
// NOTE: Currently Postgres use a separate boolean flag for null instead of datum.
typedef enum {
  YB_YQL_DATUM_STANDARD_VALUE = 0,
  YB_YQL_DATUM_LIMIT_MAX,
  YB_YQL_DATUM_LIMIT_MIN,
} YbcPgDatumKind;

typedef enum {
  kLowerPriorityRange,
  kHigherPriorityRange,
  kHighestPriority
} YbcTxnPriorityRequirement;

// PostgreSQL can represent text strings up to 1 GB minus a four-byte header.
static const int64_t kYBCMaxPostgresTextSizeBytes = 1024ll * 1024 * 1024 - 4;

// Postgres object identifier (OID) defined in Postgres' postgres_ext.h
typedef unsigned int YbcPgOid;

typedef uint64_t YbcReadPointHandle;
#define YbcInvalidReadPointHandle 0

const YbcPgTypeEntity *YBCPgFindTypeEntity(YbcPgOid type_oid);

// These OIDs are defined here to work around the build dependency problem.
// In YBCheckDefinedOids(), we have assertions to ensure that they are in sync
// with their definitions which are generated by Postgres and not available
// yet in the build process when PgGate files are compiled.
#define kInvalidOid ((YbcPgOid) 0)
#define kByteArrayOid ((YbcPgOid) 17)

// Structure to hold the values of hidden columns when passing tuple from YB to PG.
typedef struct {
  // Postgres system columns.
  uint32_t tableoid;
  uint32_t xmin;
  uint32_t cmin;
  uint32_t xmax;
  uint32_t cmax;
  uint64_t ctid;

  // Yugabyte system columns.
  uint8_t *ybctid;
  uint8_t *ybbasectid;
  uint8_t *ybuniqueidxkeysuffix;
} YbcPgSysColumns;

// Structure to hold parameters for preparing query plan.
//
// Index-related parameters are used to describe different types of scan.
//   - Sequential scan: Index parameter is not used.
//     { index_relfilenode_oid, index_only_scan}
//        = { kInvalidRelfileNodeOid, false}
//   - IndexScan:
//     { index_relfilenode_oid, index_only_scan}
//        = { IndexRelfileNodeOid, false}
//   - IndexOnlyScan:
//     { index_relfilenode_oid, index_only_scan}
//        = { IndexRelfileNodeOid, true}
//   - PrimaryIndexScan: This is a special case as YugaByte doesn't have a separated
//     primary-index database object from table object.
//       index_relfilenode_oid = TableRelfileNodeOid
//       index_only_scan = true if ROWID is wanted. Otherwise, regular rowset is wanted.
//
// embedded_idx: true when secondary index and table are sharded together.  This is the case when
// they are colocated together (by database, tablegroup, syscatalog) or copartitioned (certain
// indexes such as pgvector).  Note that this should be false in case of primary key index scan
// since the pk index and the table are same in DocDB.
// TODO(#25940): it is currently not always false for primary key index.
typedef struct {
  YbcPgOid index_relfilenode_oid;
  bool index_only_scan;
  bool embedded_idx;
  bool fetch_ybctids_only;
} YbcPgPrepareParameters;

// Structure for output value.
typedef struct YbcPgExecOutParamValue {
#ifdef __cplusplus
  const char *bfoutput = NULL;

  // The following parameters are not yet used.
  // Detailing execution status in yugabyte.
  const char *status = NULL;
  int64_t status_code = 0;

#else
  const char *bfoutput;

  // The following parameters are not yet used.
  // Detailing execution status in yugabyte.
  const char *status;
  int64_t status_code;
#endif
} YbcPgExecOutParamValue;

// Structure to hold the execution-control parameters.
typedef struct YbcPgExecParameters {
  // TODO(neil) Move forward_scan flag here.
  // Scan parameters.
  // bool is_forward_scan;

  // LIMIT parameters for executing DML read.
  // - limit_count is the value of SELECT ... LIMIT
  // - limit_offset is value of SELECT ... OFFSET
  // - limit_use_default: Although count and offset are pushed down to YugaByte from Postgres,
  //   they are not always being used to identify the number of rows to be read from DocDB.
  //   Full-scan is needed when further operations on the rows are not done by YugaByte.
  // - out_param is an output parameter of an execution while all other parameters are IN params.
  //
  //   Examples:
  //   o WHERE clause is not processed by YugaByte. All rows must be sent to Postgres code layer
  //     for filtering before LIMIT is applied.
  //   o ORDER BY clause is not processed by YugaByte. Similarly all rows must be fetched and sent
  //     to Postgres code layer.
  // For now we only support one rowmark.

#ifdef __cplusplus
  uint64_t limit_count = 0;
  uint64_t limit_offset = 0;
  bool limit_use_default = true;
  int rowmark = -1;
  // Cast these *_wait_policy fields to yb::WaitPolicy for C++ use. (2 is for yb::WAIT_ERROR)
  // Note that WAIT_ERROR has a different meaning between pg_wait_policy and docdb_wait_policy.
  // Please see the WaitPolicy enum in common.proto for details.
  int pg_wait_policy = 2;
  int docdb_wait_policy = 2;
  char *bfinstr = NULL;
  uint64_t backfill_read_time = 0;
  uint64_t* stmt_in_txn_limit_ht_for_reads = NULL;
  char *partition_key = NULL;
  struct YbPgExecOutParam *out_param = NULL;
  bool is_index_backfill = false;
  int work_mem = 4096; // Default work_mem in guc.c
  int yb_fetch_row_limit = 1024; // Default yb_fetch_row_limit in guc.c
  int yb_fetch_size_limit = 0; // Default yb_fetch_size_limit in guc.c
#else
  uint64_t limit_count;
  uint64_t limit_offset;
  bool limit_use_default;
  int rowmark;
  // Cast these *_wait_policy fields to LockWaitPolicy for C use.
  // Note that WAIT_ERROR has a different meaning between pg_wait_policy and docdb_wait_policy.
  // Please see the WaitPolicy enum in common.proto for details.
  int pg_wait_policy;
  int docdb_wait_policy;
  char *bfinstr;
  uint64_t backfill_read_time;
  uint64_t* stmt_in_txn_limit_ht_for_reads;
  char *partition_key;
  struct YbPgExecOutParam *out_param;
  bool is_index_backfill;
  int work_mem;
  int yb_fetch_row_limit;
  int yb_fetch_size_limit;
  int yb_index_check;
#endif
} YbcPgExecParameters;

typedef struct {
  bool collate_is_valid_non_c;
  const char *sortkey;
} YbcPgCollationInfo;

typedef struct {
  int attr_num;
  uint64_t datum;
  bool is_null;
  const YbcPgTypeEntity *type_entity;
  YbcPgCollationInfo collation_info;
  int collation_id;
} YbcPgAttrValueDescriptor;

typedef struct {
  uint32_t wait_event;
  uint16_t rpc_code;
} YbcWaitEventInfo;

typedef struct {
  uint32_t* wait_event;
  uint16_t* rpc_code;
} YbcWaitEventInfoPtr;

typedef struct {
  YbcPgMemctx (*GetCurrentYbMemctx)();
  const char* (*GetDebugQueryString)();
  void (*WriteExecOutParam)(struct YbPgExecOutParam *, const struct YbcPgExecOutParamValue *);
  /* yb_type.c */
  int64_t (*UnixEpochToPostgresEpoch)(int64_t);
  void (*ConstructArrayDatum)(YbcPgOid oid, const char **, const int, char **, size_t *);
  /* hba.c */
  int (*CheckUserMap)(const char *, const char *, const char *, bool case_insensitive);
  /* pgstat.h */
  YbcWaitEventInfo (*PgstatReportWaitStart)(YbcWaitEventInfo);
  YbcReadPointHandle (*GetCatalogSnapshotReadPoint)(YbcPgOid table_oid, bool create_if_not_exists);
  /* replication origin */
  uint16_t (*GetSessionReplicationOriginId)();
  /* CHECK_FOR_INTERRUPTS */
  void (*CheckForInterrupts)();
} YbcPgCallbacks;

typedef struct {
  uint64_t num_tablets;
  uint64_t num_hash_key_columns;
  bool is_colocated; /* via database or tablegroup, but not for system tables */
  YbcPgOid tablegroup_oid; /* InvalidOid if none */
  YbcPgOid colocation_id; /* 0 if not colocated */
  size_t num_range_key_columns;
  char *tablegroup_name;
} YbcTablePropertiesData;

typedef YbcTablePropertiesData *YbcTableProperties;

typedef struct {
  YbcPgOid database_oid;
  YbcPgOid table_relfilenode_oid;
  size_t nattrs;
  YbcPgAttrValueDescriptor *attrs;
} YbcPgYBTupleIdDescriptor;

typedef struct {
  const char *host;
  const char *cloud;
  const char *region;
  const char *zone;
  const char *public_ip;
  bool is_primary;
  uint16_t pg_port;
  const char *uuid;
  const char *universe_uuid;
} YbcServerDescriptor;

typedef struct {
  bool is_primary;
  bool is_hash;
} YbcPgColumnInfo;

// Hold info of range split value
typedef struct {
  uint64_t datum;
  YbcPgDatumKind datum_kind;
} YbcPgSplitDatum;

typedef enum {
  YB_YQL_BOUND_INVALID = 0,
  YB_YQL_BOUND_VALID,
  YB_YQL_BOUND_VALID_INCLUSIVE
} YbcPgBoundType;

// Must be kept in sync with PgVectorDistanceType in common.proto
typedef enum {
  YB_VEC_DIST_INVALID,
  YB_VEC_DIST_L2,
  YB_VEC_DIST_IP,
  YB_VEC_DIST_COSINE
} YbcPgVectorDistType;

// Must be kept in sync with PgVectorIndexType in common.proto
typedef enum {
  YB_VEC_INVALID,
  YB_VEC_DUMMY,
  YB_VEC_IVFFLAT,
  YB_VEC_HNSW,
} YbcPgVectorIdxType;

typedef struct {
  YbcPgVectorDistType dist_type;
  YbcPgVectorIdxType idx_type;
  uint32_t dimensions;
  uint32_t attnum;
  uint32_t hnsw_ef;
  uint32_t hnsw_m;
} YbcPgVectorIdxOptions;

typedef struct {
  uint64_t reads;
  uint64_t read_ops;
  uint64_t writes;
  uint64_t read_wait;
  uint64_t rows_scanned;
  uint64_t rows_received;
} YbcPgExecReadWriteStats;

typedef struct {
  int64_t sum;
  int64_t count;
} YbcPgExecEventMetric;

typedef struct {
  uint64_t version;
  uint64_t gauges[YB_PGGATE_IDENTIFIER(YB_STORAGE_GAUGE_COUNT)];
  int64_t counters[YB_PGGATE_IDENTIFIER(YB_STORAGE_COUNTER_COUNT)];
  YbcPgExecEventMetric
      events[YB_PGGATE_IDENTIFIER(YB_STORAGE_EVENT_COUNT)];
} YbcPgExecStorageMetrics;

typedef struct {
  YbcPgExecReadWriteStats tables;
  YbcPgExecReadWriteStats indices;
  YbcPgExecReadWriteStats catalog;

  uint64_t num_flushes;
  uint64_t flush_wait;

  YbcPgExecStorageMetrics read_metrics;
  YbcPgExecStorageMetrics write_metrics;

  uint64_t rows_removed_by_recheck;
  uint64_t commit_wait;
} YbcPgExecStats;

// Make sure this is in sync with PgsqlMetricsCaptureType in pgsql_protocol.proto.
typedef enum {
  YB_YQL_METRICS_CAPTURE_NONE = 0,
  YB_YQL_METRICS_CAPTURE_ALL = 1,
  // List of metrics to capture for PGSS metrics:
  // - kCountersForPgStatStatements, kEventStatsForPgStatStatements in tablet_metrics.cc
  // - kRegularDBTickersForPgStatStatements in docdb_statistics.cc
  YB_YQL_METRICS_CAPTURE_PGSS_METRICS = 2,
} YbcPgMetricsCaptureType;

typedef struct {
  YbcPgExecStats stats;
  bool is_timing_required;
  bool is_commit_stats_required;
  YbcPgMetricsCaptureType metrics_capture;
} YbcPgExecStatsState;

typedef struct {
  unsigned char data[16];
} YbcPgUuid;

typedef struct {
  uint64_t session_id;
  YbcPgUuid txn_id;
  bool is_not_null;
} YbcPgSessionTxnInfo;

// Values to copy from main backend session into background workers
typedef struct {
  uint64_t session_id;
  uint64_t txn_serial_no;
  uint64_t read_time_serial_no;
  uint32_t active_sub_transaction_id;
} YbcPgSessionState;

typedef struct {
  char* jwks;
  char* matching_claim_key;
  char** allowed_issuers;
  size_t allowed_issuers_length;
  char** allowed_audiences;
  size_t allowed_audiences_length;
  char* username;
  char* usermap;
} YbcPgJwtAuthOptions;

// source:
// https://github.com/gperftools/gperftools/blob/master/src/gperftools/malloc_extension.h#L154
typedef struct {
  // "generic.total_physical_bytes"
  int64_t total_physical_bytes;
  // "generic.heap_size"
  int64_t heap_size_bytes;
  // "generic.current_allocated_bytes"
  int64_t current_allocated_bytes;
  // "tcmalloc.pageheap_free_bytes"
  int64_t pageheap_free_bytes;
  // "tcmalloc.pageheap_unmapped_bytes"
  int64_t pageheap_unmapped_bytes;
} YbcTcmallocStats;

typedef struct {
  int64_t estimated_bytes;
  int64_t estimated_count;
  int64_t avg_bytes_per_allocation;
  int64_t sampled_bytes;
  int64_t sampled_count;
  char* call_stack;
  bool estimated_bytes_is_null;
  bool estimated_count_is_null;
  bool avg_bytes_per_allocation_is_null;
  bool sampled_bytes_is_null;
  bool sampled_count_is_null;
  bool call_stack_is_null;
} YbcHeapSnapshotSample;

// In per database catalog version mode, this puts a limit on the maximum
// number of databases that can exist in a cluster.
static const int32_t kYBCMaxNumDbCatalogVersions = 10000;

typedef enum {
  YB_YQL_PREFETCHER_TRUST_CACHE_AUTH,
  YB_YQL_PREFETCHER_TRUST_CACHE,
  YB_YQL_PREFETCHER_RENEW_CACHE_SOFT,
  YB_YQL_PREFETCHER_RENEW_CACHE_HARD
} YbcPgSysTablePrefetcherCacheMode;

typedef struct {
  uint64_t read;
  uint64_t local_limit;
  uint64_t global_limit;
  uint64_t in_txn_limit;
  int64_t serial_no;
} YbcReadHybridTime;

typedef struct {
  uint64_t version;
  YbcReadHybridTime version_read_time;
  bool is_db_catalog_version_mode;
} YbcPgLastKnownCatalogVersionInfo;

typedef enum {
  // Single shard transactions can use a fast path to give full ACID guarantees without the overhead
  // of a distributed transaction.
  YB_SINGLE_SHARD_TRANSACTION,
  // Force non-transactional semantics to avoid overhead of a distributed transaction. This is used
  // in the following cases as of today:
  //   (1) Index backfill
  //   (2) COPY with ysql_non_txn_copy=true or COPY to colocated table with
  //       yb_fast_path_for_colocated_copy=true.
  //   (3) For normal DML writes if yb_disable_transactional_writes is set by the user
  YB_NON_TRANSACTIONAL,
  // Use a distributed transaction for full ACID semantics (common case).
  YB_TRANSACTIONAL
} YbcPgTransactionSetting;

// Postgres WAL record pointer defined in Postgres' xlogdefs.h
typedef uint64_t YbcPgXLogRecPtr;

// Postgres Replica Identity values defined in Postgres' pg_class.h
#define YBC_REPLICA_IDENTITY_DEFAULT 'd'
#define YBC_REPLICA_IDENTITY_NOTHING 'n'
#define YBC_REPLICA_IDENTITY_FULL 'f'
#define YBC_REPLICA_IDENTITY_INDEX 'i'
#define YBC_YB_REPLICA_IDENTITY_CHANGE 'c'

typedef struct {
  YbcPgOid table_oid;
  char identity_type;
} YbcPgReplicaIdentityDescriptor;

typedef struct {
  const char *slot_name;
  const char *output_plugin;
  const char *stream_id;
  YbcPgOid database_oid;
  bool active;
  uint64_t confirmed_flush;
  uint64_t restart_lsn;
  uint32_t xmin;
  uint64_t record_id_commit_time_ht;
  YbcPgReplicaIdentityDescriptor *replica_identities;
  int replica_identities_count;
  uint64_t last_pub_refresh_time;
  const char *yb_lsn_type;
  uint64_t active_pid;
  bool expired;
  bool allow_tables_without_primary_key;
  bool detect_publication_changes_implicitly;
} YbcReplicationSlotDescriptor;

typedef struct {
  const char *stream_id;
  uint64_t confirmed_flush_lsn;
  uint64_t restart_lsn;
  uint32_t xmin;
  uint64_t record_id_commit_time_ht;
  uint64_t last_pub_refresh_time;
  uint64_t active_pid;
} YbcSlotEntryDescriptor;

// Upon adding any more palloc'd members in the below struct, add logic to free it in
// DeepFreeRecordBatch function of yb_virtual_wal_client.c.
typedef struct {
  const char* column_name;
  // Null indicates that the value is explicitly null while Omitted indicates that the value is
  // present but was just not sent from the CDC service due to the Replica Identity (CHANGE,
  // MODIFIED_COLUMNS_OLD_AND_NEW_IMAGES).
  uint64_t after_op_datum;
  bool after_op_is_null;
  bool after_op_is_omitted;
  uint64_t before_op_datum;
  bool before_op_is_null;
  bool before_op_is_omitted;
} YbcPgDatumMessage;

typedef enum {
  YB_PG_ROW_MESSAGE_ACTION_UNKNOWN = 0,
  YB_PG_ROW_MESSAGE_ACTION_BEGIN = 1,
  YB_PG_ROW_MESSAGE_ACTION_COMMIT = 2,
  YB_PG_ROW_MESSAGE_ACTION_INSERT = 3,
  YB_PG_ROW_MESSAGE_ACTION_UPDATE = 4,
  YB_PG_ROW_MESSAGE_ACTION_DELETE = 5,
  YB_PG_ROW_MESSAGE_ACTION_DDL = 6,
} YbcPgRowMessageAction;

// Upon adding any more palloc'd members in the below struct, add logic to free it in
// DeepFreeRecordBatch function of yb_virtual_wal_client.c.
typedef struct {
  int col_count;
  YbcPgDatumMessage* cols;
  // Microseconds since PostgreSQL epoch (2000-01-01). Used by most of the PG code and sent to the
  // client as part of the record.
  uint64_t commit_time;
  // The hybrid time of the commit. Used to set the correct read time for catalog changes.
  uint64_t commit_time_ht;
  YbcPgRowMessageAction action;
  // Valid for DMLs and kPgInvalidOid for other (BEGIN/COMMIT) records.
  YbcPgOid table_oid;
  // Virtual LSN and xid generated by the virtual wal.
  YbcPgXLogRecPtr lsn;
  uint32_t xid;
  // Replication origin id associated with the transaction.
  uint32_t xrepl_origin_id;
} YbcPgRowMessage;

// Upon adding any more palloc'd members in the below struct, add logic to free it in
// DeepFreeRecordBatch function of yb_virtual_wal_client.c.
typedef struct {
  int row_count;
  YbcPgRowMessage* rows;
  bool needs_publication_table_list_refresh;
  uint64_t publication_refresh_time;
} YbcPgChangeRecordBatch;

// A struct to store ASH metadata in PG's procarray
typedef struct {
  // A unique id corresponding to a YSQL query in bytes.
  unsigned char root_request_id[16];

  // Query id as seen on pg_stat_statements to identify identical
  // normalized queries. There might be many queries with different
  // root_request_id but with the same query_id.
  uint64_t query_id;

  // pid of the YSQL/YCQL backend which is executing the query
  int32_t pid;

  // OID of database.
  uint32_t database_id;

  // OID of current user.
  uint32_t user_id;

  // If addr_family is AF_INET (ipv4) or AF_INET6 (ipv6), client_addr stores
  // the ipv4/ipv6 address and client_port stores the port of the PG process
  // where the YSQL query originated. In case of AF_INET, the first 4 bytes
  // of client_addr are used to store the ipv4 address as raw bytes.
  // In case of AF_INET6, all the 16 bytes are used to store the ipv6 address
  // as raw bytes.
  // If addr_family is AF_UNIX, client_addr and client_port are nulled out.
  unsigned char client_addr[16];
  uint16_t client_port;
  uint8_t addr_family;

  // Postgres-specific memory usage in bytes. On Apple devices this falls back to
  // resident set size (RSS), since proportional set size (PSS) is not available.
  int64_t pss_mem_bytes;
} YbcAshMetadata;

typedef struct {
  int64_t queryid;
  const char* query;
  bool is_prepared;
  int64_t calls;
  double total_time;
  double min_time;
  double max_time;
  double mean_time;
  double stddev_time;
  const char* keyspace;
} YbcYCQLStatementStats;

// Struct to store ASH samples in the circular buffer.
typedef struct {
  // Metadata of the sample.
  // top_level_node_id and rpc_request_id are also part of the metadata,
  // but the reason to not store them inside YbcAshMetadata is that these remain
  // constant in PG for all the samples of a particular node. So we don't store it
  // in YbcAshMetadata, which is stored in the procarray to save shared memory.
  YbcAshMetadata metadata;

  // UUID of the TServer where the query generated.
  // This remains constant for PG samples on a node, but can differ for TServer
  // samples as TServer can be processing requests from other nodes.
  unsigned char top_level_node_id[16];

  // A single query can generate multiple RPCs, this is used to differentiate
  // those RPCs. This will always be 0 for PG samples
  int64_t rpc_request_id;

  // Auxiliary information about the sample.
  char aux_info[16];

  // 32-bit wait event code of the sample.
  uint32_t encoded_wait_event_code;

  // If a certain number of samples are available and we capture a portion of
  // them, the sample weight is the reciprocal of the captured portion or 1,
  // whichever is maximum.
  float sample_weight;

  // Timestamp when the sample was captured.
  uint64_t sample_time;
} YbcAshSample;

// A struct to pass ASH postgres config to PgClient
typedef struct {
  YbcAshMetadata* metadata;
  unsigned char top_level_node_id[16];
  // length of host should be equal to INET6_ADDRSTRLEN
  char host[46];
} YbcPgAshConfig;

typedef struct {
  uint32_t code;
  const char *description;
} YbcWaitEventDescriptor;

typedef enum {
  QUERY_ID_TYPE_DEFAULT,
  QUERY_ID_TYPE_BACKGROUND_WORKER,
  QUERY_ID_TYPE_WALSENDER,
} YbcAshConstQueryIdType;

typedef struct {
  int attr_num;
  const YbcPgTypeEntity* type_entity;
  YbcPgCollationInfo collation_info;
  bool is_null;
  uint64_t datum;
} YbcBindColumn;

// Postgres replication slot snapshot action defined in Postgres' walsender.h
// It does not include EXPORT_SNAPSHOT since it isn't supported yet.
typedef enum {
  YB_REPLICATION_SLOT_NOEXPORT_SNAPSHOT,
  YB_REPLICATION_SLOT_USE_SNAPSHOT,
  YB_REPLICATION_SLOT_EXPORT_SNAPSHOT
} YbcPgReplicationSlotSnapshotAction;

typedef enum {
  YB_REPLICATION_SLOT_LSN_TYPE_SEQUENCE,
  YB_REPLICATION_SLOT_LSN_TYPE_HYBRID_TIME
} YbcLsnType;

typedef enum {
  YB_REPLICATION_SLOT_ORDERING_MODE_ROW,
  YB_REPLICATION_SLOT_ORDERING_MODE_TRANSACTION
} YbcOrderingMode;

typedef struct {
  const char* tablet_id;
  const char* table_name;
  const char* table_id;
  const char* namespace_name;
  const char* table_type;
  const char* partition_key_start;
  size_t partition_key_start_len;
  const char* partition_key_end;
  size_t partition_key_end_len;
} YbcPgTabletsDescriptor;

typedef struct {
  YbcPgTabletsDescriptor tablet_descriptor;
  const char* tablet_data_state;
  const char* pgschema_name;
} YbcPgLocalTabletsDescriptor;

typedef struct {
  YbcPgTabletsDescriptor tablet_descriptor;
  const char** replicas;
  size_t replicas_count;
  bool is_hash_partitioned;
  const uint64_t* replica_sst_sizes;
  const uint64_t* replica_wal_sizes;
} YbcPgGlobalTabletsDescriptor;

typedef struct {
  const char* name;
  const char* value;
} YbcMetricsInfo;

typedef struct {
  const char* uuid;
  YbcMetricsInfo* metrics;
  const size_t metrics_count;
  const char* status;
  const char* error;
} YbcPgServerMetricsInfo;

typedef struct {
  YbcPgOid db_id;
  const char* db_name;
  YbcPgOid parent_db_id;
  const char* parent_db_name;
  const char* state;
  int64_t as_of_time;
  const char* failure_reason;
} YbcPgDatabaseCloneInfo;

typedef struct {
  int rowmark;
  int pg_wait_policy;
  int docdb_wait_policy;
} YbcPgExplicitRowLockParams;

typedef struct {
  bool is_initialized;
  int pg_wait_policy;
  YbcPgOid conflicting_table_id;
} YbcPgExplicitRowLockErrorInfo;

// For creating a new table...
typedef enum {
  PG_YBROWID_MODE_NONE,   // ...do not add ybrowid
  PG_YBROWID_MODE_HASH,   // ...add ybrowid HASH
  PG_YBROWID_MODE_RANGE,  // ...add ybrowid ASC
} YbcPgYbrowidMode;

// The reserved database oid for system_postgres. Must be the same as
// kPgSequencesDataTableOid (defined in entity_ids.h).
static const YbcPgOid kYBCPgSequencesDataDatabaseOid = 65535;

typedef struct {
  // The clone time in microseconds since the unix epoch (not a hybrid time).
  uint64_t clone_time;
  const char* src_db_name;
  const char* src_owner;
  const char* tgt_owner;
} YbcCloneInfo;

// A thread-safe way to cache compiled regexes.
typedef struct {
  void* array;
} YbcPgThreadLocalRegexpCache;

typedef void (*YbcPgThreadLocalRegexpCacheCleanup)(YbcPgThreadLocalRegexpCache*);


typedef struct {
  void *slot;
} YbcPgInsertOnConflictKeyInfo;

typedef enum {
  KEY_NOT_FOUND,
  KEY_READ,
  KEY_JUST_INSERTED,
} YbcPgInsertOnConflictKeyState;

typedef struct {
  uint32_t database_id;
  uint32_t classid;
  uint32_t objid;
  uint32_t objsubid;
} YbcAdvisoryLockId;

typedef enum {
  YB_ADVISORY_LOCK_SHARED,
  YB_ADVISORY_LOCK_EXCLUSIVE
} YbcAdvisoryLockMode;

typedef struct {
  YbcPgOid db_id;
  int iso_level;
  bool read_only;
} YbcPgTxnSnapshot;

typedef struct {
  uint32_t start_range;
  uint32_t end_range;
} YbcReplicationSlotHashRange;

typedef struct {
  uint32_t db_oid;
  uint32_t relation_oid;
  uint32_t object_oid;
  uint32_t object_sub_oid;
} YbcObjectLockId;

typedef enum {
  YB_OBJECT_NO_LOCK,
  YB_OBJECT_ACCESS_SHARE_LOCK,
  YB_OBJECT_ROW_SHARE_LOCK,
  YB_OBJECT_ROW_EXCLUSIVE_LOCK,
  YB_OBJECT_SHARE_UPDATE_EXCLUSIVE_LOCK,
  YB_OBJECT_SHARE_LOCK,
  YB_OBJECT_SHARE_ROW_EXCLUSIVE_LOCK,
  YB_OBJECT_EXCLUSIVE_LOCK,
  YB_OBJECT_ACCESS_EXCLUSIVE_LOCK
} YbcObjectLockMode;

// Catalog cache invalidation message list associated with one catalog version for
// a given database.
typedef struct {
  // NULL means a PG null value, which is different from a PG empty string ''.
  char* message_list;
  // num_bytes will be zero for both PG null value and a PG empty string ''.
  size_t num_bytes;
} YbcCatalogMessageList;

// A list of YbcCatalogMessageList associated with a consecutive list of catalog versions
// for a given database.
typedef struct {
  YbcCatalogMessageList* message_lists;
  int num_lists;
} YbcCatalogMessageLists;

typedef struct {
  YbcPgOid table_oid;
  uint64_t mutations;
  char* last_analyze_info;
} YbcAutoAnalyzeInfo;

typedef enum {
  /*
   * Taken from XClusterNamespaceInfoPB.XClusterRole in
   * yb/common/common_types.proto.
   */
  XCLUSTER_ROLE_UNSPECIFIED = 0,
  XCLUSTER_ROLE_UNAVAILABLE = 1,
  XCLUSTER_ROLE_NOT_AUTOMATIC_MODE = 2,
  XCLUSTER_ROLE_AUTOMATIC_SOURCE = 3,
  XCLUSTER_ROLE_AUTOMATIC_TARGET = 4,
} YbcXClusterReplicationRole;

typedef struct {
  uint8_t reason;
  uint64_t uintarg;
  YbcPgOid oidarg;
  const char* strarg1;
  const char* strarg2;
} YbcFlushDebugContext;

typedef struct {
  char data[32];
} YbcPgSharedDataPlaceholder;

typedef struct {
  const uint64_t *parallel_leader_session_id;
  YbcPgSharedDataPlaceholder *shared_data;
} YbcPgInitPostgresInfo;

typedef struct {
  int64_t xact_start_timestamp;
  bool xact_read_only;
  bool xact_deferrable;
  bool enable_tracing;
  int effective_pggate_isolation_level;
  bool read_from_followers_enabled;
  int32_t follower_read_staleness_ms;
} YbcPgInitTransactionData;

typedef struct {
  bool is_region_local;
  YbcPgOid tablespace_oid;
} YbcPgTableLocalityInfo;

// Merge sort key information
typedef struct {
  // Position of the merge sort column in the index
  uint16_t att_idx;
  // Position of the merge sort column in the table
  uint16_t value_idx;
  int (*comparator)(uint64_t datum1, bool isnull1, uint64_t datum2, bool isnull2, void *sortstate);
  void *sortstate;
} YbcSortKey;

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#undef YB_DEFINE_HANDLE_TYPE
