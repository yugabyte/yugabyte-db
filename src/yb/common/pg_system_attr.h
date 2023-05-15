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

#pragma once

namespace yb {

// In addition to regular columns, YB support for postgres also have virtual columns.
// Virtual columns are just expression that is evaluated by DocDB in "doc_expr.cc".
enum class PgSystemAttrNum : int {
  // Postgres system columns.
  kSelfItemPointer      = -1, // ctid.
  kMinTransactionId     = -2, // xmin
  kMinCommandId         = -3, // cmin
  kMaxTransactionId     = -4, // xmax
  kMaxCommandId         = -5, // cmax
  kTableOid             = -6, // tableoid

  // YugaByte system columns.
  kYBTupleId            = -7, // ybctid: virtual column representing DocDB-encoded key.
                              // YB analogue of Postgres's SelfItemPointer/ctid column.

  // The following attribute numbers are stored persistently in the table schema. For this reason,
  // they are chosen to avoid potential conflict with Postgres' own sys attributes now and future.
  kYBRowId              = -100, // ybrowid: auto-generated key-column for tables without pkey.
  kYBIdxBaseTupleId     = -101, // ybidxbasectid: for indexes ybctid of the indexed table row.
  kYBUniqueIdxKeySuffix = -102, // ybuniqueidxkeysuffix: extra key column for unique indexes, used
                                // to ensure SQL semantics for null (null != null) in DocDB
                                // (where null == null). For each index row will be set to:
                                //  - the base table ctid when one or more indexed cols are null
                                //  - to null otherwise (all indexed cols are non-null).
  kObjectId             = -104, // YB_TODO(arpan): remove it, added it temporarily for consistency
                                // with sysattr.h.
};

} // namespace yb
