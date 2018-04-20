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

// Implementation of YQLStorageIf with rocksdb as a backend. This is what all of our QL tables use.
class QLRocksDBStorage : public common::YQLStorageIf {
 public:
  explicit QLRocksDBStorage(rocksdb::DB *rocksdb);

  //------------------------------------------------------------------------------------------------
  // CQL Support.
  CHECKED_STATUS GetIterator(const QLReadRequestPB& request,
                             const Schema& projection,
                             const Schema& schema,
                             const TransactionOperationContextOpt& txn_op_context,
                             const ReadHybridTime& read_time,
                             const common::QLScanSpec& spec,
                             std::unique_ptr<common::YQLRowwiseIteratorIf> *iter) const override;

  CHECKED_STATUS BuildYQLScanSpec(const QLReadRequestPB& request,
                                  const ReadHybridTime& read_time,
                                  const Schema& schema,
                                  bool include_static_columns,
                                  const Schema& static_projection,
                                  std::unique_ptr<common::QLScanSpec>* spec,
                                  std::unique_ptr<common::QLScanSpec>* static_row_spec,
                                  ReadHybridTime* req_read_time) const override;

  //------------------------------------------------------------------------------------------------
  // PGSQL Support.
  virtual CHECKED_STATUS GetIterator(const PgsqlReadRequestPB& request,
                                     const Schema& projection,
                                     const Schema& schema,
                                     const TransactionOperationContextOpt& txn_op_context,
                                     const ReadHybridTime& read_time,
                                     const common::PgsqlScanSpec& spec,
                                     common::YQLRowwiseIteratorIf::UniPtr* iter) const override;

  virtual CHECKED_STATUS BuildYQLScanSpec(const PgsqlReadRequestPB& request,
                                          const ReadHybridTime& read_time,
                                          const Schema& schema,
                                          std::unique_ptr<common::PgsqlScanSpec>* spec,
                                          ReadHybridTime* req_read_time) const override;

 private:
  rocksdb::DB *const rocksdb_;
};

}  // namespace docdb
}  // namespace yb
#endif // YB_DOCDB_QL_ROCKSDB_STORAGE_H
