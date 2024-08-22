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

#include <limits>
#include <string>
#include <type_traits>

#include "yb/common/common_fwd.h"
#include "yb/common/read_hybrid_time.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/docdb_statistics.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/util/monotime.h"
#include "yb/util/operation_counter.h"

namespace yb::docdb {

// An interface to support various different storage backends for a QL table.
class YQLStorageIf {
 public:
  typedef std::unique_ptr<YQLStorageIf> UniPtr;
  typedef std::shared_ptr<YQLStorageIf> SharedPtr;

  virtual ~YQLStorageIf() {}

  //------------------------------------------------------------------------------------------------
  // CQL Support.
  virtual Status GetIterator(
      const QLReadRequestPB& request,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      const qlexpr::QLScanSpec& spec,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      std::unique_ptr<YQLRowwiseIteratorIf>* iter) const = 0;

  virtual Status BuildYQLScanSpec(
      const QLReadRequestPB& request,
      const ReadHybridTime& read_time,
      const Schema& schema,
      bool include_static_columns,
      std::unique_ptr<qlexpr::QLScanSpec>* spec,
      std::unique_ptr<qlexpr::QLScanSpec>* static_row_spec) const = 0;

  //------------------------------------------------------------------------------------------------
  // PGSQL Support.

  // TODO(neil) Need to replace GetIterator(ybctid) with CreateIterator and InitIterator.
  // I leave Create and Init code here, so I don't have to rewrite them in the near future.
  //
  // - Replacement is not done yet because reusing iterator is not yet allowed. When reusing it,
  //   doc_key.get_next() asserts that an infinite loop is detected even though we're calling
  //   get_next() for a different hash codes.
  // - Create and init can be used to create iterator once and initialize with different ybctid for
  //   different execution.
  // - Doc_key needs to be changed to allow reusing iterator.
  virtual Status CreateIterator(
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      std::unique_ptr<YQLRowwiseIteratorIf>* iter) const = 0;

  virtual Status InitIterator(
      DocRowwiseIterator* doc_iter,
      const PgsqlReadRequestPB& request,
      const Schema& schema,
      const QLValuePB& ybctid) const = 0;

  // Create iterator for querying by partition and range key.
  virtual Status GetIterator(
      const PgsqlReadRequestPB& request,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      const dockv::DocKey& start_doc_key,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      std::unique_ptr<YQLRowwiseIteratorIf>* iter) const = 0;

  // Create iterator for querying by ybctid.
  virtual Status GetIteratorForYbctid(
      uint64 stmt_id,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const ReadOperationData& read_operation_data,
      const Slice& min_ybctid,
      const Slice& max_ybctid,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      std::unique_ptr<YQLRowwiseIteratorIf>* iter,
      SkipSeek skip_seek = SkipSeek::kFalse) const = 0;

  virtual std::string ToString() const = 0;
};

}  // namespace yb::docdb
