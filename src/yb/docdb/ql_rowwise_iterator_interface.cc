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

#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/util/result.h"

namespace yb {
namespace docdb {

Status YQLRowwiseIteratorIf::GetNextReadSubDocKey(SubDocKey* sub_doc_key) const {
  return Status::OK();
}

Result<Slice> YQLRowwiseIteratorIf::GetTupleId() const {
  return STATUS(NotSupported, "This iterator does not provide tuple id");
}

Result<bool> YQLRowwiseIteratorIf::SeekTuple(const Slice& tuple_id) {
  return STATUS(NotSupported, "This iterator cannot seek by tuple id");
}

Status YQLRowwiseIteratorIf::NextRow(const Schema& projection, QLTableRow* table_row) {
  return DoNextRow(projection, table_row);
}

Status YQLRowwiseIteratorIf::NextRow(QLTableRow* table_row) {
  return DoNextRow(schema(), table_row);
}

}  // namespace docdb
}  // namespace yb
