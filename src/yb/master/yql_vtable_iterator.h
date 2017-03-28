// Copyright (c) YugaByte, Inc.

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

  CHECKED_STATUS NextRow(YQLValueMap* value_map) override;

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
