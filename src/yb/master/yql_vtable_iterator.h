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

#pragma once

#include "yb/common/ql_protocol.pb.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"

namespace yb {
namespace master {

// An iterator over a YQLVirtualTable.
class YQLVTableIterator : public docdb::YQLRowwiseIteratorIf {
 public:
  // hashed_column_values - is used to filter rows, i.e. if hashed_column_values is not empty
  // only rows starting with specified hashed columns will be iterated.
  YQLVTableIterator(
      std::shared_ptr<qlexpr::QLRowBlock> vtable,
      const google::protobuf::RepeatedPtrField<QLExpressionPB>& hashed_column_values);

  virtual ~YQLVTableIterator();

  Result<bool> DoFetchNext(
      qlexpr::QLTableRow* table_row,
      const dockv::ReaderProjection* projection,
      qlexpr::QLTableRow* static_row,
      const dockv::ReaderProjection* static_projection) override;

  Result<bool> PgFetchNext(dockv::PgTableRow* table_row) override;

  std::string ToString() const override;

  Result<HybridTime> RestartReadHt() override;

 private:
  void Advance(bool increment);

  std::shared_ptr<qlexpr::QLRowBlock> vtable_;
  size_t vtable_index_ = 0;
  const google::protobuf::RepeatedPtrField<QLExpressionPB>& hashed_column_values_;
};

}  // namespace master
}  // namespace yb
