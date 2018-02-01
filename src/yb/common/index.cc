//--------------------------------------------------------------------------------------------------
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
// Classes that implement secondary index.
//--------------------------------------------------------------------------------------------------

#include "yb/common/index.h"
#include "yb/common/common.pb.h"

using std::vector;
using google::protobuf::RepeatedPtrField;

namespace yb {

IndexInfo::IndexColumn::IndexColumn(const IndexInfoPB::IndexColumnPB& pb)
    : column_id(ColumnId(pb.column_id())),
      indexed_column_id(ColumnId(pb.indexed_column_id())) {
}

namespace {

vector<IndexInfo::IndexColumn> IndexColumnFromPB(
    const RepeatedPtrField<IndexInfoPB::IndexColumnPB>& columns) {
  vector<IndexInfo::IndexColumn> cols;
  cols.reserve(columns.size());
  for (const auto& column : columns) {
    cols.emplace_back(column);
  }
  return cols;
}

} // namespace

IndexInfo::IndexInfo(const IndexInfoPB& pb)
    : table_id_(pb.table_id()),
      schema_version_(pb.version()),
      is_local_(pb.is_local()),
      columns_(IndexColumnFromPB(pb.columns())),
      hash_column_count_(pb.hash_column_count()),
      range_column_count_(pb.range_column_count()) {
}

}  // namespace yb
