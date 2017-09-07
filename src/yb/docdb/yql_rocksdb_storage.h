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

#ifndef YB_DOCDB_YQL_ROCKSDB_STORAGE_H
#define YB_DOCDB_YQL_ROCKSDB_STORAGE_H

#include "rocksdb/include/rocksdb/db.h"
#include "yb/common/yql_rowwise_iterator_interface.h"
#include "yb/common/yql_storage_interface.h"

namespace yb {
namespace docdb {

// Implementation of YQLStorageIf with rocksdb as a backend. This is what all of our YQL tables use.
class YQLRocksDBStorage : public common::YQLStorageIf {

 public:
  explicit YQLRocksDBStorage(rocksdb::DB *rocksdb);
  CHECKED_STATUS GetIterator(const YQLReadRequestPB& request,
                             const Schema& projection,
                             const Schema& schema,
                             HybridTime req_hybrid_time,
                             std::unique_ptr<common::YQLRowwiseIteratorIf> *iter) const override;
  CHECKED_STATUS BuildYQLScanSpec(const YQLReadRequestPB& request,
                                  const HybridTime& hybrid_time,
                                  const Schema& schema,
                                  bool include_static_columns,
                                  const Schema& static_projection,
                                  std::unique_ptr<common::YQLScanSpec>* spec,
                                  std::unique_ptr<common::YQLScanSpec>* static_row_spec,
                                  HybridTime* req_hybrid_time) const override;
 private:
  rocksdb::DB *const rocksdb_;
};

}  // namespace docdb
}  // namespace yb
#endif // YB_DOCDB_YQL_ROCKSDB_STORAGE_H
