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

#include <string>
#include <vector>

#include "yb/docdb/docdb_fwd.h"
#include "yb/rocksdb/cache.h"

#include "yb/common/common_types.pb.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/deadline_info.h"
#include "yb/docdb/docdb_types.h"
#include "yb/docdb/expiration.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace docdb {

class IntentAwareIterator;

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

// This version of GetSubDocument creates a new iterator every time. This is not recommended for
// multiple calls to subdocs that are sequential or near each other, in e.g. doc_rowwise_iterator.
Result<boost::optional<SubDocument>> TEST_GetSubDocument(
    const Slice& sub_doc_key,
    const DocDB& doc_db,
    const rocksdb::QueryId query_id,
    const TransactionOperationContext& txn_op_context,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time = ReadHybridTime::Max(),
    const std::vector<KeyEntryValue>* projection = nullptr);

// This class reads SubDocument instances for a given table. The caller should initialize with
// UpdateTableTombstoneTime and SetTableTtl, if applicable, before calling Get(). Instances
// of DocDBTableReader assume, for the lifetime of the instance, that the provided
// IntentAwareIterator is either pointed to a requested row, or before it, or that row does not
// exist. Care should be taken to ensure this assumption is not broken for callers independently
// modifying the provided IntentAwareIterator.
// projection - contains list of subkeys that should be read. Unlisted columns are skipped during
// scan.
class DocDBTableReader {
 public:
  DocDBTableReader(
      IntentAwareIterator* iter, CoarseTimePoint deadline,
      const std::vector<KeyEntryValue>* projection,
      TableType table_type,
      std::reference_wrapper<const SchemaPackingStorage> schema_packing_storage);

  // Updates expiration/overwrite data based on table tombstone time. If the table is not a
  // colocated table as indicated by the provided root_doc_key, this method is a no-op.
  Status UpdateTableTombstoneTime(const Slice& root_doc_key);

  // Determine based on the provided schema if there is a table-level TTL and use the computed value
  // in any subsequently read SubDocuments. This call also turns on row-level TTL tracking for
  // subsequently read SubDocuments.
  void SetTableTtl(const Schema& table_schema);

  // Read value (i.e. row), identified by root_doc_key to result.
  // Returns true if value was found, false otherwise.
  Result<bool> Get(const Slice& root_doc_key, SubDocument* result);

 private:
  // Initializes the reader to read a row at sub_doc_key by seeking to and reading obsolescence info
  // at that row.
  Status InitForKey(const Slice& sub_doc_key);

  class GetHelper;

  // Owned by caller.
  IntentAwareIterator* iter_;
  DeadlineInfo deadline_info_;
  const std::vector<KeyEntryValue>* projection_;
  const TableType table_type_;
  const SchemaPackingStorage& schema_packing_storage_;

  std::vector<KeyBytes> encoded_projection_;
  DocHybridTime table_tombstone_time_ = DocHybridTime::kMin;
  Expiration table_expiration_;
};

}  // namespace docdb
}  // namespace yb
