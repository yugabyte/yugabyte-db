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
#define YB_DEFINE_HANDLE_TYPE(name) typedef class yb::pggate::name *YBC##name

#else
#define YB_DEFINE_HANDLE_TYPE(name) typedef struct name *YBC##name
#endif  // __cplusplus

#ifdef __cplusplus
#include "yb/yql/pggate/pggate.h"
extern "C" {
#endif  // __cplusplus

// TODO(neil) Hanlde to Env. Each Postgres process might need just one ENV, maybe more.
YB_DEFINE_HANDLE_TYPE(PgEnv);

// Handle to a session. Postgres should create one YBCPgSession per client connection.
YB_DEFINE_HANDLE_TYPE(PgSession);

// Handle to a statement.
YB_DEFINE_HANDLE_TYPE(PgStatement);

// Use YugaByte datatype numeric representation for now.
// TODO(neil) This should be change to "PgType *" and convert Postgres's TypeName struct to our
// class PgType or QLType.
typedef int YBCPgDataType;

#ifdef __cplusplus
}  // extern "C"
#endif

#undef YB_DEFINE_HANDLE_TYPE

#endif  // YB_YQL_PGGATE_YBC_PG_TYPEDEFS_H
