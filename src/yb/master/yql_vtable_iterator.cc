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

#include "yb/master/yql_vtable_iterator.h"

namespace yb {
namespace master {

YQLVTableIterator::YQLVTableIterator(std::unique_ptr<QLRowBlock> vtable)
    : vtable_(std::move(vtable)),
      vtable_index_(0) {
}

Status YQLVTableIterator::Init() {
  return STATUS(NotSupported, "YQLVTableIterator::Init(ScanSpec*) not supported!");
}

Status YQLVTableIterator::Init(const common::QLScanSpec& spec) {
  // As of 04/17, we don't use the scanspec for simplicity.
  return Status::OK();
}

Status YQLVTableIterator::DoNextRow(const Schema& projection, QLTableRow* table_row) {
  if (vtable_index_ >= vtable_->row_count()) {
    return STATUS(NotFound, "No more rows left!");
  }

  // TODO: return columns in projection only.
  QLRow& row = vtable_->row(vtable_index_);
  for (int i = 0; i < row.schema().num_columns(); i++) {
    table_row->AllocColumn(row.schema().column_id(i),
                           down_cast<const QLValue&>(row.column(i)));
  }
  vtable_index_++;
  return Status::OK();
}

void YQLVTableIterator::SkipRow() {
  if (vtable_index_ < vtable_->row_count()) {
    vtable_index_++;
  }
}

CHECKED_STATUS YQLVTableIterator::SetPagingStateIfNecessary(const QLReadRequestPB& request,
                                                            QLResponsePB* response) const {
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

YQLVTableIterator::~YQLVTableIterator() {
}

}  // namespace master
}  // namespace yb
