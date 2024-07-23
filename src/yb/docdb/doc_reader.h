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
#include "yb/dockv/expiration.h"
#include "yb/dockv/subdocument.h"
#include "yb/dockv/value.h"

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb::docdb {

class IntentAwareIterator;

YB_DEFINE_ENUM(DocReaderResult, (kNotFound)(kFoundAndFinished)(kFoundNotFinished));

// Get table tombstone time from doc_db. If the table is not a colocated table as indicated
// by the provided root_doc_key, this method returns DocHybridTime::kInvalid.
Result<DocHybridTime> GetTableTombstoneTime(
    Slice root_doc_key, const DocDB& doc_db,
    const TransactionOperationContext& txn_op_context,
    const ReadOperationData& read_operation_data);

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
Result<std::optional<dockv::SubDocument>> TEST_GetSubDocument(
    Slice sub_doc_key,
    const DocDB& doc_db,
    const rocksdb::QueryId query_id,
    const TransactionOperationContext& txn_op_context,
    const ReadOperationData& read_operation_data,
    const dockv::ReaderProjection* projection = nullptr);

class PackedRowData;

struct DocDBTableReaderData final {
  // Owned by caller.
  IntentAwareIterator* iter;
  DeadlineInfo& deadline_info;
  const dockv::ReaderProjection* projection;
  const TableType table_type;
  const dockv::SchemaPackingStorage& schema_packing_storage;
  std::unique_ptr<PackedRowData> packed_row;
  const Schema& schema;

  std::vector<dockv::KeyBytes> encoded_projection;
  EncodedDocHybridTime table_tombstone_time{EncodedDocHybridTime::kMin};
  dockv::Expiration table_expiration;

 private:
  const bool use_fast_backward_scan;

  DocDBTableReaderData(
      IntentAwareIterator* iter_, DeadlineInfo& deadline,
      const dockv::ReaderProjection* projection_,
      TableType table_type_,
      std::reference_wrapper<const dockv::SchemaPackingStorage> schema_packing_storage_,
      std::reference_wrapper<const Schema> schema, bool use_fast_backward_scan_ = false);
  ~DocDBTableReaderData();

  friend class DocDBTableReader;
};

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
  template<class... Args>
  explicit DocDBTableReader(Args&&... args) : data_(std::forward<Args>(args)...) {
    Init();
  }

  ~DocDBTableReader();

  // Updates expiration/overwrite data based on table tombstone time.
  // If the given doc_ht is DocHybridTime::kInvalid, this method is a no-op.
  Status UpdateTableTombstoneTime(DocHybridTime doc_ht);

  // Determine based on the provided schema if there is a table-level TTL and use the computed value
  // in any subsequently read SubDocuments. This call also turns on row-level TTL tracking for
  // subsequently read SubDocuments.
  void SetTableTtl(const Schema& table_schema);

  // Read value (i.e. row), identified by root_doc_key to result.
  // Returns true if value was found, false otherwise.
  // FetchedEntry will contain last entry fetched by the iterator.
  Result<DocReaderResult> Get(
      KeyBuffer* root_doc_key, const FetchedEntry& fetched_entry, dockv::SubDocument* result);

  // Same as get, but for rows that have doc keys with only one subkey.
  // This is always true for YSQL.
  // result shouldn't be nullptr and will be filled with the same number of primitives as number of
  // columns passed to ctor in projection and in the same order.
  Result<DocReaderResult> GetFlat(
      KeyBuffer* root_doc_key, const FetchedEntry& fetched_entry, qlexpr::QLTableRow* result);
  Result<DocReaderResult> GetFlat(
      KeyBuffer* root_doc_key, const FetchedEntry& fetched_entry, dockv::PgTableRow* result);

 private:
  void Init();

  // Initializes the reader to read a row at sub_doc_key by seeking to and reading obsolescence info
  // at that row.
  Status InitForKey(Slice sub_doc_key);

  DocDBTableReaderData data_;
};

}  // namespace yb::docdb
