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
enum YBCPgDataType {
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
};

typedef enum YBCPgDataType YBCPgDataType;

// Postgres object identifier (OID) defined in Postgres' postgres_ext.h
typedef unsigned int YBCPgOid;
#define YBCPgInvalidOid ((YBCPgOid) 0)

// Postgres pg_catalog oid defined in Postgres' pg_namespace.h
#define YBCPgCatalogOid ((YBCPgOid) 11)

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#undef YB_DEFINE_HANDLE_TYPE

#endif  // YB_YQL_PGGATE_YBC_PG_TYPEDEFS_H
