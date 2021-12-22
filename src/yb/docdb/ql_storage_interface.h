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

#ifndef YB_DOCDB_QL_STORAGE_INTERFACE_H
#define YB_DOCDB_QL_STORAGE_INTERFACE_H

#include <limits>
#include <string>
#include <type_traits>

#include "yb/common/common_fwd.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/util/monotime.h"

namespace yb {
namespace docdb {

// An interface to support various different storage backends for a QL table.
class YQLStorageIf {
 public:
  typedef std::unique_ptr<YQLStorageIf> UniPtr;
  typedef std::shared_ptr<YQLStorageIf> SharedPtr;

  virtual ~YQLStorageIf() {}

  //------------------------------------------------------------------------------------------------
  // CQL Support.
  virtual CHECKED_STATUS GetIterator(const QLReadRequestPB& request,
                                     const Schema& projection,
                                     const Schema& schema,
                                     const TransactionOperationContext& txn_op_context,
                                     CoarseTimePoint deadline,
                                     const ReadHybridTime& read_time,
                                     const QLScanSpec& spec,
                                     std::unique_ptr<YQLRowwiseIteratorIf>* iter) const = 0;

  virtual CHECKED_STATUS BuildYQLScanSpec(
      const QLReadRequestPB& request,
      const ReadHybridTime& read_time,
      const Schema& schema,
      bool include_static_columns,
      const Schema& static_projection,
      std::unique_ptr<QLScanSpec>* spec,
      std::unique_ptr<QLScanSpec>* static_row_spec) const = 0;

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
  virtual CHECKED_STATUS CreateIterator(const Schema& projection,
                                        const Schema& schema,
                                        const TransactionOperationContext& txn_op_context,
                                        CoarseTimePoint deadline,
                                        const ReadHybridTime& read_time,
                                        std::unique_ptr<YQLRowwiseIteratorIf>* iter) const = 0;

  virtual CHECKED_STATUS InitIterator(docdb::YQLRowwiseIteratorIf* doc_iter,
                                      const PgsqlReadRequestPB& request,
                                      const Schema& schema,
                                      const QLValuePB& ybctid) const = 0;

  // Create iterator for querying by partition and range key.
  virtual CHECKED_STATUS GetIterator(const PgsqlReadRequestPB& request,
                                     const Schema& projection,
                                     const Schema& schema,
                                     const TransactionOperationContext& txn_op_context,
                                     CoarseTimePoint deadline,
                                     const ReadHybridTime& read_time,
                                     const docdb::DocKey& start_doc_key,
                                     std::unique_ptr<YQLRowwiseIteratorIf>* iter) const = 0;

  // Create iterator for querying by ybctid.
  virtual CHECKED_STATUS GetIterator(uint64 stmt_id,
                                     const Schema& projection,
                                     const Schema& schema,
                                     const TransactionOperationContext& txn_op_context,
                                     CoarseTimePoint deadline,
                                     const ReadHybridTime& read_time,
                                     const QLValuePB& ybctid,
                                     std::unique_ptr<YQLRowwiseIteratorIf>* iter) const = 0;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_QL_STORAGE_INTERFACE_H
