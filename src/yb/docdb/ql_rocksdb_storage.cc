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

#include "yb/docdb/ql_rocksdb_storage.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_expr.h"

namespace yb {
namespace docdb {

QLRocksDBStorage::QLRocksDBStorage(rocksdb::DB *rocksdb)
    : rocksdb_(rocksdb) {

}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLRocksDBStorage::GetIterator(
    const QLReadRequestPB& request,
    const Schema& projection,
    const Schema& schema,
    const TransactionOperationContextOpt& txn_op_context,
    MonoTime deadline,
    const ReadHybridTime& read_time,
    const common::QLScanSpec& spec,
    std::unique_ptr<common::YQLRowwiseIteratorIf> *iter) const {

  auto doc_iter = std::make_unique<DocRowwiseIterator>(
      projection, schema, txn_op_context, rocksdb_, deadline, read_time);
  RETURN_NOT_OK(doc_iter->Init(spec));
  *iter = std::move(doc_iter);
  return Status::OK();
}

CHECKED_STATUS QLRocksDBStorage::BuildYQLScanSpec(const QLReadRequestPB& request,
                                                  const ReadHybridTime& read_time,
                                                  const Schema& schema,
                                                  const bool include_static_columns,
                                                  const Schema& static_projection,
                                                  std::unique_ptr<common::QLScanSpec>* spec,
                                                  std::unique_ptr<common::QLScanSpec>*
                                                  static_row_spec,
                                                  ReadHybridTime* req_read_time) const {
  // Populate dockey from QL key columns.
  int32_t hash_code = request.has_hash_code() ?
      static_cast<docdb::DocKeyHash>(request.hash_code()) : -1;
  int32_t max_hash_code = request.has_max_hash_code() ?
      static_cast<docdb::DocKeyHash>(request.max_hash_code()) : -1;

  vector<PrimitiveValue> hashed_components;
  RETURN_NOT_OK(QLKeyColumnValuesToPrimitiveValues(
      request.hashed_column_values(), schema, 0, schema.num_hash_key_columns(),
      &hashed_components));

  *req_read_time = read_time;
  SubDocKey start_sub_doc_key;
  // Decode the start SubDocKey from the paging state and set scan start key and hybrid time.
  if (request.has_paging_state() &&
      request.paging_state().has_next_row_key() &&
      !request.paging_state().next_row_key().empty()) {

    KeyBytes start_key_bytes(request.paging_state().next_row_key());
    RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
    DCHECK_EQ(req_read_time->read, req_read_time->local_limit);
    DCHECK_EQ(req_read_time->read, req_read_time->global_limit);
    *req_read_time = ReadHybridTime::SingleTime(start_sub_doc_key.hybrid_time());
    // TODO(dtxn) What should we do with read_limit_ht here?

    // If we start the scan with a specific primary key, the normal scan spec we return below will
    // not include the static columns if any for the start key. We need to return a separate scan
    // spec to fetch those static columns.
    const DocKey& start_doc_key = start_sub_doc_key.doc_key();
    if (include_static_columns && !start_doc_key.range_group().empty()) {
      const DocKey hashed_doc_key(start_doc_key.hash(), start_doc_key.hashed_group());
      static_row_spec->reset(new DocQLScanSpec(static_projection, hashed_doc_key,
          request.query_id(), request.is_forward_scan()));
    }
  }

  // Construct the scan spec basing on the WHERE condition.
  spec->reset(new DocQLScanSpec(schema, hash_code, max_hash_code, hashed_components,
      request.has_where_expr() ? &request.where_expr().condition() : nullptr,
      request.query_id(), request.is_forward_scan(), include_static_columns,
      start_sub_doc_key.doc_key()));
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

CHECKED_STATUS QLRocksDBStorage::GetIterator(
    const PgsqlReadRequestPB& request,
    const Schema& projection,
    const Schema& schema,
    const TransactionOperationContextOpt& txn_op_context,
    MonoTime deadline,
    const ReadHybridTime& read_time,
    const common::PgsqlScanSpec& spec,
    common::YQLRowwiseIteratorIf::UniPtr* iter) const {

  auto doc_iter = std::make_unique<DocRowwiseIterator>(
      projection, schema, txn_op_context, rocksdb_, deadline, read_time);
  RETURN_NOT_OK(doc_iter->Init(spec));

  *iter = std::move(doc_iter);
  return Status::OK();
}

CHECKED_STATUS QLRocksDBStorage::BuildYQLScanSpec(const PgsqlReadRequestPB& request,
                                                  const ReadHybridTime& read_time,
                                                  const Schema& schema,
                                                  common::PgsqlScanSpec::UniPtr* spec,
                                                  ReadHybridTime* req_read_time) const {
  // Populate dockey from QL key columns.
  int32_t hash_code = request.has_hash_code() ?
      static_cast<docdb::DocKeyHash>(request.hash_code()) : -1;
  int32_t max_hash_code = request.has_max_hash_code() ?
      static_cast<docdb::DocKeyHash>(request.max_hash_code()) : -1;
  vector<PrimitiveValue> hashed_components;
  RETURN_NOT_OK(InitKeyColumnPrimitiveValues(request.hashed_column_values(),
                                             schema,
                                             0,
                                             &hashed_components));

  *req_read_time = read_time;
  SubDocKey start_sub_doc_key;
  // Decode the start SubDocKey from the paging state and set scan start key and hybrid time.
  if (request.has_paging_state() &&
      request.paging_state().has_next_row_key() &&
      !request.paging_state().next_row_key().empty()) {
    KeyBytes start_key_bytes(request.paging_state().next_row_key());
    RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
    req_read_time->read = start_sub_doc_key.hybrid_time();
  }

  // Construct the scan spec basing on the WHERE condition.
  spec->reset(new DocPgsqlScanSpec(schema,
                                   request.stmt_id(),
                                   hashed_components,
                                   hash_code,
                                   max_hash_code,
                                   request.has_where_expr() ? &request.where_expr() : nullptr,
                                   start_sub_doc_key.doc_key(),
                                   request.is_forward_scan()));
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
