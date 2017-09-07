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

#ifndef YB_MASTER_YQL_VTABLE_ITERATOR_H
#define YB_MASTER_YQL_VTABLE_ITERATOR_H

#include "yb/common/yql_rowwise_iterator_interface.h"
#include "yb/common/yql_scanspec.h"
#include "yb/docdb/doc_key.h"

namespace yb {
namespace master {

// An iterator over a YQLVirtualTable.
class YQLVTableIterator : public common::YQLRowwiseIteratorIf {
 public:
  explicit YQLVTableIterator(const std::unique_ptr<YQLRowBlock> vtable);
  CHECKED_STATUS Init(ScanSpec *spec) override;

  CHECKED_STATUS Init(const common::YQLScanSpec& spec) override;

  CHECKED_STATUS NextBlock(RowBlock *dst) override;

  CHECKED_STATUS NextRow(const Schema& projection, YQLTableRow* table_row) override;

  // Virtual table does not contain any static column.
  bool IsNextStaticColumn() const override { return false; }

  void SkipRow() override;

  CHECKED_STATUS SetPagingStateIfNecessary(const YQLReadRequestPB& request,
                                           const YQLRowBlock& rowblock,
                                           const size_t row_count_limit,
                                           YQLResponsePB* response) const override;

  bool HasNext() const override;

  std::string ToString() const override;

  const Schema &schema() const override;

  void GetIteratorStats(std::vector<IteratorStats>* stats) const override;

  virtual ~YQLVTableIterator();
 private:
  std::unique_ptr<YQLRowBlock> vtable_;
  size_t vtable_index_;
};

}  // namespace master
}  // namespace yb
#endif // YB_MASTER_YQL_VTABLE_ITERATOR_H
