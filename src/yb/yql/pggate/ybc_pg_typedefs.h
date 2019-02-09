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

#ifndef YB_YQL_PGGATE_YBC_PG_TYPEDEFS_H
#define YB_YQL_PGGATE_YBC_PG_TYPEDEFS_H

#include <stddef.h>
#include "yb/util/ybc_util.h"

#ifdef __cplusplus

#define YB_DEFINE_HANDLE_TYPE(name) \
    namespace yb { \
    namespace pggate { \
    class name; \
    } \
    } \
    typedef class yb::pggate::name *YBC##name;

#else
#define YB_DEFINE_HANDLE_TYPE(name) typedef struct name *YBC##name;
#endif  // __cplusplus

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// TODO(neil) Handle to Env. Each Postgres process might need just one ENV, maybe more.
YB_DEFINE_HANDLE_TYPE(PgEnv)

// Handle to a session. Postgres should create one YBCPgSession per client connection.
YB_DEFINE_HANDLE_TYPE(PgSession)

// Handle to a statement.
YB_DEFINE_HANDLE_TYPE(PgStatement)

// Handle to an expression.
YB_DEFINE_HANDLE_TYPE(PgExpr);

// Handle to a table description
YB_DEFINE_HANDLE_TYPE(PgTableDesc);

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
  YB_YQL_DATA_TYPE_UINT64 = 103
} YBCPgDataType;

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

  // YugaByte storgate (DocDB) type.
  YBCPgDataType yb_type;

  // Allow to be used for primary key.
  bool allow_for_primary_key;

  // Converting Postgres datum to YugaByte expression.
  YBCPgDatumToData datum_to_yb;

  // Converting YugaByte values to Postgres in-memory-formatted datum.
  YBCPgDatumFromData yb_to_datum;
} YBCPgTypeEntity;

// API to read type information.
const YBCPgTypeEntity *YBCPgFindTypeEntity(int type_oid);
YBCPgDataType YBCPgGetType(const YBCPgTypeEntity *type_entity);
bool YBCPgAllowForPrimaryKey(const YBCPgTypeEntity *type_entity);

// PostgreSQL can represent text strings up to 1 GB minus a four-byte header.
static const int64_t kYBCMaxPostgresTextSizeBytes = 1024ll * 1024 * 1024 - 4;

// Postgres object identifier (OID) defined in Postgres' postgres_ext.h
typedef unsigned int YBCPgOid;
#define kInvalidOid ((YBCPgOid) 0)

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

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#undef YB_DEFINE_HANDLE_TYPE

#endif  // YB_YQL_PGGATE_YBC_PG_TYPEDEFS_H
