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

#ifndef YB_DOCDB_QL_ROCKSDB_STORAGE_H
#define YB_DOCDB_QL_ROCKSDB_STORAGE_H

#include <boost/optional.hpp>

#include "yb/rocksdb/db.h"
#include "yb/common/ql_rowwise_iterator_interface.h"
#include "yb/common/ql_storage_interface.h"

namespace yb {
namespace docdb {

// Implementation of QLStorageIf with rocksdb as a backend. This is what all of our QL tables use.
class QLRocksDBStorage : public common::QLStorageIf {

 public:
  explicit QLRocksDBStorage(rocksdb::DB *rocksdb);
  CHECKED_STATUS GetIterator(const QLReadRequestPB& request,
                             const Schema& projection,
                             const Schema& schema,
                             const TransactionOperationContextOpt& txn_op_context,
                             HybridTime req_hybrid_time,
                             std::unique_ptr<common::QLRowwiseIteratorIf> *iter) const override;
  CHECKED_STATUS BuildQLScanSpec(const QLReadRequestPB& request,
                                  const HybridTime& hybrid_time,
                                  const Schema& schema,
                                  bool include_static_columns,
                                  const Schema& static_projection,
                                  std::unique_ptr<common::QLScanSpec>* spec,
                                  std::unique_ptr<common::QLScanSpec>* static_row_spec,
                                  HybridTime* req_hybrid_time) const override;
 private:
  rocksdb::DB *const rocksdb_;
};

}  // namespace docdb
}  // namespace yb
#endif // YB_DOCDB_QL_ROCKSDB_STORAGE_H
