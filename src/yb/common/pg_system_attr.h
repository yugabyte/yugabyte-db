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

  // After PG11, PG removed WITH OIDS support. This change made oids into normal columns and made a
  // number of changes to sysattr.h. (Constants referenced in this comment are from sysattr.h.) The
  // change removed the special oid system column attribute number of -2. It also renumbered the
  // system attributes [-3,-7] to [-2,-6] to fill the gap. YB had set the value of
  // YBTupleIdAttributeNumber to be equal to PG's FirstLowInvalidHeapAttributeNumber, but that value
  // changed from -8 to -7.
  //
  // During a PG major version upgrade, we require for the two versions of PG to interoperate, so we
  // must preserve -8 as the protocol value for the tuple ID in DocDB for all PG versions.
  // Unfortunately, we can't just naively switch PG code back to using -8 for the YB tuple ID field,
  // because there are many bitmapsets that store negative values starting with
  // FirstLowInvalidHeapAttributeNumber, which is -7. Therefore, internal to PG, it's easiest to use
  // a similar scheme to PG11, where kYBTupleIdAttributeNumber is equal to
  // FirstLowInvalidAttributeNumber, allowing it to work in PG bitmapsets, and then when querying
  // DocDB, we convert it to the protocol value of -8.
  kPGInternalYBTupleId  = -7, // Value of PG's internal representation of YB tuple ID.
  kYBTupleId            = -8, // ybctid: virtual column representing DocDB-encoded key.
                              // YB analogue of Postgres's SelfItemPointer/ctid column.
                              // Must be -8 in all versions of YB as part of the wire protocol.

  // The following attribute numbers are stored persistently in the table schema. For this reason,
  // they are chosen to avoid potential conflict with Postgres' own sys attributes now and future.
  kYBRowId              = -100, // ybrowid: auto-generated key-column for tables without pkey.
  kYBIdxBaseTupleId     = -101, // ybidxbasectid: for indexes ybctid of the indexed table row.
  kYBUniqueIdxKeySuffix = -102, // ybuniqueidxkeysuffix: extra key column for unique indexes, used
                                // to ensure SQL semantics for null (null != null) in DocDB
                                // (where null == null). For each index row will be set to:
                                //  - the base table ctid when one or more indexed cols are null
                                //  - to null otherwise (all indexed cols are non-null).
};

} // namespace yb
