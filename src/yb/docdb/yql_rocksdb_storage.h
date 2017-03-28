// Copyright (c) YugaByte, Inc.

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
  CHECKED_STATUS GetIterator(const Schema& projection, const Schema& schema,
                             HybridTime req_hybrid_time,
                             std::unique_ptr<common::YQLRowwiseIteratorIf> *iter) const override;
  CHECKED_STATUS BuildYQLScanSpec(const YQLReadRequestPB& request,
                                  const HybridTime& hybrid_time,
                                  const Schema& schema,
                                  std::unique_ptr<common::YQLScanSpec>* spec,
                                  HybridTime* req_hybrid_time) const override;
 private:
  rocksdb::DB *const rocksdb_;
};

}  // namespace docdb
}  // namespace yb
#endif // YB_DOCDB_YQL_ROCKSDB_STORAGE_H
