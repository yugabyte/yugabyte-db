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
// Portions Copyright (c) 1996-2017, PostgreSQL Global Development Group
// Portions Copyright (c) 1994, Regents of the University of California
//
// This module defines PostgreSQL format that should be used to represent YugaByte datatype when
// communicating with PostgreSQL clients.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_YBPOSTGRES_PG_TYPE_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PG_TYPE_H_

#include "yb/common/ql_type.h"
#include "yb/yql/pgsql/ybpostgres/pg_defs.h"

namespace yb {
namespace pgapi {

#define BOOLOID                 16
#define BYTEAOID                17
#define CHAROID                 18
#define NAMEOID                 19
#define INT8OID                 20
#define INT2OID                 21
#define INT2VECTOROID           22
#define INT4OID                 23
#define REGPROCOID              24
#define TEXTOID                 25
#define OIDOID                  26
#define TIDOID                  27
#define XIDOID                  28
#define CIDOID                  29
#define OIDVECTOROID            30
#define POINTOID                600
#define LSEGOID                 601
#define PATHOID                 602
#define BOXOID                  603
#define POLYGONOID              604
#define LINEOID                 628
#define FLOAT4OID               700
#define FLOAT8OID               701
#define ABSTIMEOID              702
#define RELTIMEOID              703
#define TINTERVALOID            704
#define UNKNOWNOID              705
#define CIRCLEOID               718
#define CASHOID                 790
#define INETOID                 869
#define CIDROID                 650
#define BPCHAROID               1042
#define VARCHAROID              1043
#define DATEOID                 1082
#define TIMEOID                 1083
#define TIMESTAMPOID            1114
#define TIMESTAMPTZOID          1184
#define INTERVALOID             1186
#define TIMETZOID               1266
#define ZPBITOID                1560
#define VARBITOID               1562
#define NUMERICOID              1700
#define REFCURSOROID            1790
#define REGPROCEDUREOID         2202
#define REGOPEROID              2203
#define REGOPERATOROID          2204
#define REGCLASSOID             2205
#define REGTYPEOID              2206
#define REGROLEOID              4096
#define REGNAMESPACEOID         4089
#define REGTYPEARRAYOID         2211
#define UUIDOID                 2950
#define LSNOID                  3220
#define TSVECTOROID             3614
#define GTSVECTOROID            3642
#define TSQUERYOID              3615
#define REGCONFIGOID            3734
#define REGDICTIONARYOID        3769
#define JSONBOID                3802
#define INT4RANGEOID            3904

PGOid PgTypeId(DataType data_type);
int16_t PgTypeLength(DataType data_type);

}  // namespace pgapi
}  // namespace yb

#endif  // YB_YQL_PGSQL_YBPOSTGRES_PG_TYPE_H_
