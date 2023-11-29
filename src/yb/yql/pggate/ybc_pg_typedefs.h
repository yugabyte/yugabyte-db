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
    typedef class yb::pggate::name *YBC##name;

#define YB_PGGATE_IDENTIFIER(name) yb::pggate::name

#else
#define YB_DEFINE_HANDLE_TYPE(name) typedef struct name *YBC##name;
#define YB_PGGATE_IDENTIFIER(name) name
#endif  // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Handle to a session. Postgres should create one YBCPgSession per client connection.
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
typedef enum PgDataType {
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
} YBCPgDataType;

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
// - YBCPgTypeEntity is used to keep static information of a datatype.
// - YBCPgTypeAttrs is used to keep customizable information of a datatype.
//
// Example:
//   For type CHAR(20), its associated YugaByte internal type (YB_YQL_DATA_TYPE_STRING) is
//   static while its typemod (size 20) can be customized for each usage.
typedef struct PgTypeAttrs {
  // Currently, we only need typmod, but we might need more datatype information in the future.
  // For example, array dimensions might be needed.
  int32_t typmod;
} YBCPgTypeAttrs;

// Datatype conversion functions.
typedef void (*YBCPgDatumToData)(uint64_t datum, void *ybdata, int64_t *bytes);
typedef uint64_t (*YBCPgDatumFromData)(const void *ybdata, int64_t bytes,
                                       const YBCPgTypeAttrs *type_attrs);
typedef struct PgTypeEntity {
  // Postgres type OID.
  int type_oid;

  // YugaByte storage (DocDB) type.
  YBCPgDataType yb_type;

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
  YBCPgDatumToData datum_to_yb;

  // Converting YugaByte values to Postgres in-memory-formatted datum.
  YBCPgDatumFromData yb_to_datum;
} YBCPgTypeEntity;

// Kind of a datum.
// In addition to datatype, a "datum" is also specified by "kind".
// - Standard value.
// - MIN limit value, which can be infinite, represents an absolute mininum value of a datatype.
// - MAX limit value, which can be infinite, represents an absolute maximum value of a datatype.
//
// NOTE: Currently Postgres use a separate boolean flag for null instead of datum.
typedef enum PgDatumKind {
  YB_YQL_DATUM_STANDARD_VALUE = 0,
  YB_YQL_DATUM_LIMIT_MAX,
  YB_YQL_DATUM_LIMIT_MIN,
} YBCPgDatumKind;

typedef enum TxnPriorityRequirement {
  kLowerPriorityRange,
  kHigherPriorityRange,
  kHighestPriority
} TxnPriorityRequirement;

// API to read type information.
const YBCPgTypeEntity *YBCPgFindTypeEntity(int type_oid);
YBCPgDataType YBCPgGetType(const YBCPgTypeEntity *type_entity);
bool YBCPgAllowForPrimaryKey(const YBCPgTypeEntity *type_entity);

// PostgreSQL can represent text strings up to 1 GB minus a four-byte header.
static const int64_t kYBCMaxPostgresTextSizeBytes = 1024ll * 1024 * 1024 - 4;

// Postgres object identifier (OID) defined in Postgres' postgres_ext.h
typedef unsigned int YBCPgOid;

// These OIDs are defined here to work around the build dependency problem.
// In YBCheckDefinedOids(), we have assertions to ensure that they are in sync
// with their definitions which are generated by Postgres and not available
// yet in the build process when PgGate files are compiled.
#define kInvalidOid ((YBCPgOid) 0)
#define kByteArrayOid ((YBCPgOid) 17)

// Structure to hold the values of hidden columns when passing tuple from YB to PG.
typedef struct PgSysColumns {
  // Postgres system columns.
  uint32_t oid;
  uint32_t tableoid;
  uint32_t xmin;
  uint32_t cmin;
  uint32_t xmax;
  uint32_t cmax;
  uint64_t ctid;

  // Yugabyte system columns.
  uint8_t *ybctid;
  uint8_t *ybbasectid;
} YBCPgSysColumns;

// Structure to hold parameters for preparing query plan.
//
// Index-related parameters are used to describe different types of scan.
//   - Sequential scan: Index parameter is not used.
//     { index_oid, index_only_scan, use_secondary_index } = { kInvalidOid, false, false }
//   - IndexScan:
//     { index_oid, index_only_scan, use_secondary_index } = { IndexOid, false, true }
//   - IndexOnlyScan:
//     { index_oid, index_only_scan, use_secondary_index } = { IndexOid, true, true }
//   - PrimaryIndexScan: This is a special case as YugaByte doesn't have a separated
//     primary-index database object from table object.
//       index_oid = TableOid
//       index_only_scan = true if ROWID is wanted. Otherwise, regular rowset is wanted.
//       use_secondary_index = false
//
// Attribute "querying_colocated_table"
//   - If 'true', SELECT from colocated tables (of any type - database, tablegroup, system).
//   - Note that the system catalogs are specifically for Postgres API and not Yugabyte
//     system-tables.
typedef struct PgPrepareParameters {
  YBCPgOid index_oid;
  bool index_only_scan;
  bool use_secondary_index;
  bool querying_colocated_table;
} YBCPgPrepareParameters;

// Opaque type for output parameter.
typedef struct YbPgExecOutParam PgExecOutParam;

// Structure for output value.
typedef struct PgExecOutParamValue {
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
typedef struct PgExecParameters {
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
  PgExecOutParam *out_param = NULL;
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
  PgExecOutParam *out_param;
  bool is_index_backfill;
  int work_mem;
  int yb_fetch_row_limit;
  int yb_fetch_size_limit;
#endif
} YBCPgExecParameters;

typedef struct PgCollationInfo {
  bool collate_is_valid_non_c;
  const char *sortkey;
} YBCPgCollationInfo;

typedef struct PgAttrValueDescriptor {
  int attr_num;
  uint64_t datum;
  bool is_null;
  const YBCPgTypeEntity *type_entity;
  YBCPgCollationInfo collation_info;
  int collation_id;
} YBCPgAttrValueDescriptor;

typedef struct PgCallbacks {
  YBCPgMemctx (*GetCurrentYbMemctx)();
  const char* (*GetDebugQueryString)();
  void (*WriteExecOutParam)(PgExecOutParam *, const YbcPgExecOutParamValue *);
  void (*SignalWaitStart)(uint32_t);
  void (*SignalWaitEnd)();
  void (*ProcSetTopLevelNodeId)(const uint64_t*);
  void (*ProcSetTopLevelRequestId)(const uint64_t*);
  /* yb_type.c */
  int64_t (*UnixEpochToPostgresEpoch)(int64_t);
  void (*ConstructArrayDatum)(YBCPgOid oid, const char **, const int, char **, size_t *);
  /* hba.c */
  int (*CheckUserMap)(const char *, const char *, const char *, bool case_insensitive);
} YBCPgCallbacks;

typedef struct PgGFlagsAccessor {
  const bool*     log_ysql_catalog_versions;
  const bool*     ysql_catalog_preload_additional_tables;
  const bool*     ysql_disable_index_backfill;
  const bool*     ysql_disable_server_file_access;
  const bool*     ysql_enable_reindex;
  const int32_t*  ysql_max_read_restart_attempts;
  const int32_t*  ysql_max_write_restart_attempts;
  const int32_t*  ysql_num_databases_reserved_in_db_catalog_version_mode;
  const int32_t*  ysql_output_buffer_size;
  const int32_t*  ysql_sequence_cache_minval;
  const uint64_t* ysql_session_max_batch_size;
  const bool*     ysql_sleep_before_retry_on_txn_conflict;
  const bool*     ysql_colocate_database_by_default;
  const bool*     ysql_enable_read_request_caching;
  const bool*     ysql_enable_profile;
  const bool*     ysql_disable_global_impact_ddl_statements;
  const bool*     ysql_minimal_catalog_caches_preload;
  const bool*     ysql_enable_create_database_oid_collision_retry;
  const char*     ysql_catalog_preload_additional_table_list;
} YBCPgGFlagsAccessor;

typedef struct YbTablePropertiesData {
  uint64_t num_tablets;
  uint64_t num_hash_key_columns;
  bool is_colocated; /* via database or tablegroup, but not for system tables */
  YBCPgOid tablegroup_oid; /* InvalidOid if none */
  YBCPgOid colocation_id; /* 0 if not colocated */
  size_t num_range_key_columns;
} YbTablePropertiesData;

typedef struct YbTablePropertiesData* YbTableProperties;

typedef struct PgYBTupleIdDescriptor {
  YBCPgOid database_oid;
  YBCPgOid table_oid;
  size_t nattrs;
  YBCPgAttrValueDescriptor *attrs;
} YBCPgYBTupleIdDescriptor;

typedef struct PgServerDescriptor {
  const char *host;
  const char *cloud;
  const char *region;
  const char *zone;
  const char *public_ip;
  bool is_primary;
  uint16_t pg_port;
  const char *uuid;
} YBCServerDescriptor;

typedef struct AUHMetadataDescriptor {
  const uint64_t* top_level_request_id;
  uint32_t client_node_host;
  uint16_t client_node_port;
  const uint64_t* top_level_node_id;
  int64_t current_request_id;
  int64_t query_id;
  uint8_t component;
} YBCAUHMetadataDescriptor;

typedef struct AUHAuxDescriptor {
  const char* table_id;
  const char* tablet_id;
  const char* method;
} YBCAUHAuxDescriptor;

typedef struct AUHDescriptor {
  YBCAUHMetadataDescriptor metadata;
  uint64_t wait_status_code;
  YBCAUHAuxDescriptor aux_info;
  const char* wait_status_code_as_string;
} YBCAUHDescriptor;

typedef struct NamespaceIdentifierPB {
  const char* id;
  const char* name;
  const char* database_type;
} NamespaceIdentifierPB;

typedef struct ColocatedInfo {
  bool colocated;
  const char* parent_table_id;
} ColocatedInfo;

typedef struct TableIDMetadataInfo {
  const char* table_id;
  const char* table_name;
  const char* table_type;
  const char* relation_type;
  NamespaceIdentifierPB namespace_;
  const char* pgschema_name;
  ColocatedInfo colocated_info;
} YBCTableIDMetadataInfo;

typedef struct TableIDInfo {
  YBCTableIDMetadataInfo metadata;
} YBCTableIDInfo;

typedef struct PgColumnInfo {
  bool is_primary;
  bool is_hash;
} YBCPgColumnInfo;

// Hold info of range split value
typedef struct PgRangeSplitDatum {
  uint64_t datum;
  YBCPgDatumKind datum_kind;
} YBCPgSplitDatum;

typedef enum PgBoundType {
  YB_YQL_BOUND_INVALID = 0,
  YB_YQL_BOUND_VALID,
  YB_YQL_BOUND_VALID_INCLUSIVE
} YBCPgBoundType;

typedef struct PgExecReadWriteStats {
  uint64_t reads;
  uint64_t writes;
  uint64_t read_wait;
} YBCPgExecReadWriteStats;

typedef struct PgExecStats {
  YBCPgExecReadWriteStats tables;
  YBCPgExecReadWriteStats indices;
  YBCPgExecReadWriteStats catalog;

  uint64_t num_flushes;
  uint64_t flush_wait;

  uint64_t storage_metrics[YB_PGGATE_IDENTIFIER(YB_ANALYZE_METRIC_COUNT)];
} YBCPgExecStats;

// Make sure this is in sync with PgsqlMetricsCaptureType in pgsql_protocol.proto.
typedef enum PgMetricsCaptureType {
  YB_YQL_METRICS_CAPTURE_NONE = 0,
  YB_YQL_METRICS_CAPTURE_ALL = 1,
} YBCPgMetricsCaptureType;

typedef struct PgExecStatsState {
  YBCPgExecStats stats;
  bool is_timing_required;
  YBCPgMetricsCaptureType metrics_capture;
} YBCPgExecStatsState;

typedef struct PgUuid {
  unsigned char data[16];
} YBCPgUuid;

typedef struct PgSessionTxnInfo {
  uint64_t session_id;
  YBCPgUuid txn_id;
  bool is_not_null;
} YBCPgSessionTxnInfo;

typedef struct PgJwtAuthOptions {
  char* jwks;
  char* matching_claim_key;
  char** allowed_issuers;
  size_t allowed_issuers_length;
  char** allowed_audiences;
  size_t allowed_audiences_length;
  char* username;
  char* usermap;
} YBCPgJwtAuthOptions;

// source:
// https://github.com/gperftools/gperftools/blob/master/src/gperftools/malloc_extension.h#L154
typedef struct YbTcmallocStats {
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
} YbTcmallocStats;

// In per database catalog version mode, this puts a limit on the maximum
// number of databases that can exist in a cluster.
static const int32_t kYBCMaxNumDbCatalogVersions = 10000;

typedef enum PgSysTablePrefetcherCacheMode {
  YB_YQL_PREFETCHER_TRUST_CACHE,
  YB_YQL_PREFETCHER_RENEW_CACHE_SOFT,
  YB_YQL_PREFETCHER_RENEW_CACHE_HARD,
  YB_YQL_PREFETCHER_NO_CACHE
} YBCPgSysTablePrefetcherCacheMode;

typedef struct PgLastKnownCatalogVersionInfo {
  uint64_t version;
  bool is_db_catalog_version_mode;
} YBCPgLastKnownCatalogVersionInfo;

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#undef YB_DEFINE_HANDLE_TYPE
