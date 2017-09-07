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

#ifndef YB_TABLET_KEY_VALUE_ITERATOR_H
#define YB_TABLET_KEY_VALUE_ITERATOR_H

#include "yb/common/iterator.h"
#include "yb/tablet/mvcc.h"

#include "rocksdb/db.h"

namespace yb {
namespace tablet {

// Iterates over a key-value table backed by RocksDB. Currently this only supports start/stop rows.
// No predicate testing of any kind is supported. This class is not thread-safe.
class KeyValueIterator : public RowwiseIterator {
 public:

  KeyValueIterator(const Schema* projection, rocksdb::DB* db);
  virtual ~KeyValueIterator();

  virtual CHECKED_STATUS Init(ScanSpec *spec) override;
  virtual bool HasNext() const override;

  virtual string ToString() const override {
    return "KeyValueIterator";
  }

  virtual const Schema& schema() const override {
    return *projection_;
  }

  virtual CHECKED_STATUS NextBlock(RowBlock *dst) override;
  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const override;

 private:
  const Schema* const projection_;
  rocksdb::DB* db_;
  std::unique_ptr<rocksdb::Iterator> db_iter_;

  // A copy of the exclusive upper bound key of the scan range (if any).
  bool has_upper_bound_key_;
  string exclusive_upper_bound_key_;

  // This is set when we've determined there are no more items to return to avoid doing the check
  // multiple times.
  mutable bool done_;
};

}
}

#endif
