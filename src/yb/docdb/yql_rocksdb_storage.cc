// Copyright (c) YugaByte, Inc.

#include "yb/docdb/yql_rocksdb_storage.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/docdb_util.h"
#include "yb/docdb/doc_yql_scanspec.h"

namespace yb {
namespace docdb {

YQLRocksDBStorage::YQLRocksDBStorage(rocksdb::DB *rocksdb)
    : rocksdb_(rocksdb) {

}

CHECKED_STATUS YQLRocksDBStorage::GetIterator(const YQLReadRequestPB& request,
                                              const Schema& projection,
                                              const Schema& schema,
                                              HybridTime req_hybrid_time,
                                              std::unique_ptr<common::YQLRowwiseIteratorIf> *iter)
                                              const {
  iter->reset(new DocRowwiseIterator(projection, schema, rocksdb_, req_hybrid_time));
  return Status::OK();
}

CHECKED_STATUS YQLRocksDBStorage::BuildYQLScanSpec(const YQLReadRequestPB& request,
                                                   const HybridTime& hybrid_time,
                                                   const Schema& schema,
                                                   const bool include_static_columns,
                                                   const Schema& static_projection,
                                                   std::unique_ptr<common::YQLScanSpec>* spec,
                                                   std::unique_ptr<common::YQLScanSpec>*
                                                   static_row_spec,
                                                   HybridTime* req_hybrid_time)
                                                   const {
  // Populate dockey from YQL key columns.
  docdb::DocKeyHash hash_code = static_cast<docdb::DocKeyHash>(request.hash_code());
  vector<PrimitiveValue> hashed_components;
  RETURN_NOT_OK(YQLColumnValuesToPrimitiveValues(
      request.hashed_column_values(), schema, 0, schema.num_hash_key_columns(),
      &hashed_components));

  *req_hybrid_time = hybrid_time;
  SubDocKey start_sub_doc_key;
  // Decode the start SubDocKey from the paging state and set scan start key and hybrid time.
  if (request.has_paging_state() &&
      request.paging_state().has_next_row_key() &&
      !request.paging_state().next_row_key().empty()) {
    KeyBytes start_key_bytes(request.paging_state().next_row_key());
    RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));
    *req_hybrid_time = start_sub_doc_key.hybrid_time();

    // If we start the scan with a specific primary key, the normal scan spec we return below will
    // not include the static columns if any for the start key. We need to return a separate scan
    // spec to fetch those static columns.
    const DocKey& start_doc_key = start_sub_doc_key.doc_key();
    if (include_static_columns && !start_doc_key.range_group().empty()) {
      const DocKey hashed_doc_key(start_doc_key.hash(), start_doc_key.hashed_group());
      static_row_spec->reset(new DocYQLScanSpec(static_projection, hashed_doc_key));
    }
  }

  // Construct the scan spec basing on the WHERE condition.
  spec->reset(new DocYQLScanSpec(
      schema, hash_code, hashed_components,
      request.has_where_expr() ? &request.where_expr().condition() : nullptr,
      include_static_columns, start_sub_doc_key.doc_key()));
  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
