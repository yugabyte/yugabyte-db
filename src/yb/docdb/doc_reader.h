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

#ifndef YB_DOCDB_DOC_READER_H_
#define YB_DOCDB_DOC_READER_H_

#include <string>
#include <vector>

#include "yb/docdb/docdb_fwd.h"
#include "yb/rocksdb/cache.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_types.h"
#include "yb/docdb/expiration.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value.h"
#include "yb/docdb/subdocument.h"

#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace docdb {

class IntentAwareIterator;

// Pass data to GetSubDocument function.
struct GetSubDocumentData {
  GetSubDocumentData(
    const Slice& subdoc_key,
    SubDocument* result_,
    bool* doc_found_ = nullptr,
    MonoDelta default_ttl = Value::kMaxTtl,
    DocHybridTime* table_tombstone_time_ = nullptr)
      : subdocument_key(subdoc_key),
        result(result_),
        doc_found(doc_found_),
        exp(default_ttl),
        table_tombstone_time(table_tombstone_time_) {}

  Slice subdocument_key;
  SubDocument* result;
  bool* doc_found;

  DeadlineInfo* deadline_info = nullptr;

  // The TTL and hybrid time are return values external to the SubDocument
  // which occasionally need to be accessed for TTL calculation.
  mutable Expiration exp;

  // Hybrid time of latest table tombstone.  Used by colocated tables to compare with the write
  // times of records belonging to the table.
  DocHybridTime* table_tombstone_time;

  std::string ToString() const {
    return Format("{ subdocument_key: $0 exp.ttl: $1 exp.write_time: $2 table_tombstone_time: $3 }",
                  SubDocKey::DebugSliceToString(subdocument_key), exp.ttl,
                  exp.write_ht, table_tombstone_time);
  }
};

inline std::ostream& operator<<(std::ostream& out, const GetSubDocumentData& data) {
  return out << data.ToString();
}

// Returns the whole SubDocument below some node identified by subdocument_key.
// subdocument_key should not have a timestamp.
// Before the function is called, if seek_fwd_suffices is true, the iterator is expected to be
// positioned on or before the first key when called.
// After this, the iter should be positioned just outside the considered data range. If low_subkey,
// and high_subkey are specified, the iterator will be positioned just past high_subkey. Otherwise,
// the iterator will be positioned just past the SubDocument.
// This function works with or without object init markers present.
// If tombstone and other values are inserted at the same timestamp, it results in undefined
// behavior.
// The projection, if set, restricts the scan to a subset of keys in the first level.
// The projection is used for QL selects to get only a subset of columns.
yb::Status GetSubDocument(
    IntentAwareIterator *db_iter,
    const GetSubDocumentData& data,
    const std::vector<PrimitiveValue>* projection = nullptr,
    SeekFwdSuffices seek_fwd_suffices = SeekFwdSuffices::kTrue);

// This version of GetSubDocument creates a new iterator every time. This is not recommended for
// multiple calls to subdocs that are sequential or near each other, in e.g. doc_rowwise_iterator.
yb::Status GetSubDocument(
    const DocDB& doc_db,
    const GetSubDocumentData& data,
    const rocksdb::QueryId query_id,
    const TransactionOperationContextOpt& txn_op_context,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time = ReadHybridTime::Max());

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_READER_H_
