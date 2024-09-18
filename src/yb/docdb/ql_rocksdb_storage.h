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

#include <functional>
#include <memory>

#include "yb/docdb/key_bounds.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"
#include "yb/docdb/ql_storage_interface.h"

#include "yb/util/status.h"

namespace yb {
namespace docdb {

// Implementation of YQLStorageIf with rocksdb as a backend. This is what all of our QL tables use.
class QLRocksDBStorage : public YQLStorageIf {
 public:
  explicit QLRocksDBStorage(const DocDB& doc_db);

  //------------------------------------------------------------------------------------------------
  // CQL Support.
  Status GetIterator(
      const QLReadRequestPB& request,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      const qlexpr::QLScanSpec& spec,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      std::unique_ptr<YQLRowwiseIteratorIf> *iter) const override;

  Status BuildYQLScanSpec(
      const QLReadRequestPB& request,
      const ReadHybridTime& read_time,
      const Schema& schema,
      bool include_static_columns,
      std::unique_ptr<qlexpr::QLScanSpec>* spec,
      std::unique_ptr<qlexpr::QLScanSpec>* static_row_spec) const override;

  //------------------------------------------------------------------------------------------------
  // PGSQL Support.
  Status CreateIterator(
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const docdb::DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      YQLRowwiseIteratorIf::UniPtr* iter) const override;

  Status InitIterator(DocRowwiseIterator* doc_iter,
                      const PgsqlReadRequestPB& request,
                      const Schema& schema,
                      const QLValuePB& ybctid) const override;

  Status GetIterator(
      const PgsqlReadRequestPB& request,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      const dockv::DocKey& start_doc_key,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      YQLRowwiseIteratorIf::UniPtr* iter) const override;

  Status GetIteratorForYbctid(
      uint64 stmt_id,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      const Slice& min_ybctid,
      const Slice& max_ybctid,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      YQLRowwiseIteratorIf::UniPtr* iter,
      SkipSeek skip_seek = SkipSeek::kFalse) const override;

  std::string ToString() const override;

 private:
  const DocDB doc_db_;
};

}  // namespace docdb
}  // namespace yb
