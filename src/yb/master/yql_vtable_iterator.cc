// Copyright (c) YugaByte, Inc.

#include "yb/master/yql_vtable_iterator.h"

namespace yb {
namespace master {

YQLVTableIterator::YQLVTableIterator(std::unique_ptr<YQLRowBlock> vtable)
    : vtable_(std::move(vtable)),
      vtable_index_(0) {
}

Status YQLVTableIterator::Init(ScanSpec* spec) {
  return STATUS(NotSupported, "YQLVTableIterator::Init(ScanSpec*) not supported!");
}

Status YQLVTableIterator::Init(const common::YQLScanSpec& spec) {
  // As of 04/17, we don't use the scanspec for simplicity.
  return Status::OK();
}

CHECKED_STATUS YQLVTableIterator::NextBlock(RowBlock *dst) {
  return STATUS(NotSupported, "YQLVTableIterator::NextBlock(RowBlock*) not supported!");
}

CHECKED_STATUS YQLVTableIterator::NextRow(const Schema& projection, YQLTableRow* table_row) {
  if (vtable_index_ >= vtable_->row_count()) {
    return STATUS(NotFound, "No more rows left!");
  }

  // TODO: return columns in projection only.
  YQLRow& row = vtable_->row(vtable_index_);
  for (int i = 0; i < row.schema().num_columns(); i++) {
    (*table_row)[row.schema().column_id(i)].value =
        down_cast<const YQLValueWithPB&>(row.column(i)).value();
  }
  vtable_index_++;
  return Status::OK();
}

void YQLVTableIterator::SkipRow() {
  if (vtable_index_ < vtable_->row_count()) {
    vtable_index_++;
  }
}

CHECKED_STATUS YQLVTableIterator::SetPagingStateIfNecessary(const YQLReadRequestPB& request,
                                                            const YQLRowBlock& rowblock,
                                                            const size_t row_count_limit,
                                                            YQLResponsePB* response) const {
  // We don't support paging in virtual tables.
  return Status::OK();
}

bool YQLVTableIterator::HasNext() const {
  return vtable_index_ < vtable_->row_count();
}

std::string YQLVTableIterator::ToString() const {
  return "YQLVTableIterator";
}

const Schema& YQLVTableIterator::schema() const {
  return vtable_->schema();
}

void YQLVTableIterator::GetIteratorStats(std::vector<IteratorStats>* stats) const {
  // Not supported.
}

YQLVTableIterator::~YQLVTableIterator() {
}

}  // namespace master
}  // namespace yb
