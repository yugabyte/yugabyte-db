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

#ifdef __cplusplus

extern "C" {
//--------------------------------------------------------------------------------------------------
// C++ version of the definition.
//--------------------------------------------------------------------------------------------------

// State-controlling variables.

// Status / Error.
typedef enum {
  YBC_PGERROR_SUCCESS = 0,
  YBC_PGERROR_NOTFOUND = 100,

  YBC_PGERROR_FAILURE = -1,
  YBC_PGERROR_INVALID_SESSION = -2,
  YBC_PGERROR_INVALID_HANDLE = -3,
} YBCPgError;

typedef int YBCPgErrorCode;

// TODO(neil) Hanlde to Env. Each Postgres process might need just one ENV, maybe more.
typedef class PgEnv *YBCPgEnv;

// Handle to a session. Postgres should create one YBCPgSession per client connection.
typedef class PgSession *YBCPgSession;

// Handle to a statement.
typedef class PgStatement *YBCPgStatement;

// Use YugaByte datatype numeric representation for now.
// TODO(neil) This should be change to "PgType *" and convert Postgres's TypeName struct to our
// class PgType or QLType.
typedef int YBCPgDataType;

} // extern "C"

#else
//--------------------------------------------------------------------------------------------------
// C version of the definition.
//--------------------------------------------------------------------------------------------------

// Status / Error.
typedef enum {
  YBC_PGERROR_SUCCESS = 0,
  YBC_PGERROR_NOTFOUND = 100,

  YBC_PGERROR_FAILURE = -1,
  YBC_PGERROR_INVALID_SESSION = -2,
  YBC_PGERROR_INVALID_HANDLE = -3,
} YBCPgError;

typedef int YBCPgErrorCode;

// TODO(neil) Hanlde to Env. Each Postgres process might need just one ENV, maybe more.
typedef struct PgEnv *YBCPgEnv;

// Handle to a session. Postgres should create one YBCPgSession per client connection.
typedef struct PgSession *YBCPgSession;

// Handle to a statement.
typedef struct PgStatement *YBCPgStatement;

// Use YugaByte datatype numeric representation for now.
// TODO(neil) This should be change to "PgType *" and convert Postgres's TypeName struct to our
// class PgType or QLType.
typedef int YBCPgDataType;

#endif

#endif  // YB_YQL_PGGATE_YBC_PG_TYPEDEFS_H
