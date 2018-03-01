//--------------------------------------------------------------------------------------------------
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
//
// This module contains #typdefs, #DEFINEs, and other generic definitions in Postgresql.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_YBPOSTGRES_PGDEFS_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PGDEFS_H_

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

#include "yb/util/status.h"
#include "yb/gutil/strings/substitute.h"

namespace yb {
namespace pgapi {

//--------------------------------------------------------------------------------------------------
/*
 * Common Postgres datatypes (as used in the catalogs)
 */
typedef float float4;
typedef double float8;

//--------------------------------------------------------------------------------------------------
// Macros for name length. Changing this requires an initdb.
constexpr int NAMEDATALEN = 64;

/* msb for char */
constexpr int HIGHBIT = (0x80);
#define IS_HIGHBIT_SET(ch) ((unsigned char)(ch) & HIGHBIT)

//--------------------------------------------------------------------------------------------------
/*
 * Object ID is a fundamental type in Postgres.
 */
typedef uint32_t PGOid;

#define InvalidPGOid (PGOid(0))

#define PGOID_MAX  UINT_MAX

#define atopgoid(x) ((PGOid)strtoul((x), NULL, 10))

/*
 * Identifiers of error message fields.  Kept here to keep common
 * between frontend and backend, and also to export them to libpq
 * applications.
 */
#define PG_DIAG_SEVERITY                'S'
#define PG_DIAG_SEVERITY_NONLOCALIZED 'V'
#define PG_DIAG_SQLSTATE                'C'
#define PG_DIAG_MESSAGE_PRIMARY 'M'
#define PG_DIAG_MESSAGE_DETAIL  'D'
#define PG_DIAG_MESSAGE_HINT    'H'
#define PG_DIAG_STATEMENT_POSITION 'P'
#define PG_DIAG_INTERNAL_POSITION 'p'
#define PG_DIAG_INTERNAL_QUERY  'q'
#define PG_DIAG_CONTEXT                 'W'
#define PG_DIAG_SCHEMA_NAME             's'
#define PG_DIAG_TABLE_NAME              't'
#define PG_DIAG_COLUMN_NAME             'c'
#define PG_DIAG_DATATYPE_NAME   'd'
#define PG_DIAG_CONSTRAINT_NAME 'n'
#define PG_DIAG_SOURCE_FILE             'F'
#define PG_DIAG_SOURCE_LINE             'L'
#define PG_DIAG_SOURCE_FUNCTION 'R'

/* macros for representing SQLSTATE strings compactly */
#define PGSIXBIT(ch)    (((ch) - '0') & 0x3F)
#define PGUNSIXBIT(val) (((val) & 0x3F) + '0')

#define MAKE_SQLSTATE(ch1, ch2, ch3, ch4, ch5)      \
        (PGSIXBIT(ch1) + (PGSIXBIT(ch2) << 6) + (PGSIXBIT(ch3) << 12) + \
         (PGSIXBIT(ch4) << 18) + (PGSIXBIT(ch5) << 24))

/* These macros depend on the fact that '0' becomes a zero in SIXBIT */
#define ERRCODE_TO_CATEGORY(ec)  ((ec) & ((1 << 12) - 1))
#define ERRCODE_IS_CATEGORY(ec)  (((ec) & ~((1 << 12) - 1)) == 0)

}  // namespace pgapi
}  // namespace yb

#endif  // YB_YQL_PGSQL_YBPOSTGRES_PGDEFS_H_
