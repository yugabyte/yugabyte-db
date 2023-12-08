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

#include "yb/common/hybrid_time.h"

#include "yb/util/result.h"

namespace yb {
namespace docdb {

Status YQLRowwiseIteratorIf::GetNextReadSubDocKey(dockv::SubDocKey* sub_doc_key) {
  return Status::OK();
}

Slice YQLRowwiseIteratorIf::GetTupleId() const {
  LOG(DFATAL) << "This iterator does not provide tuple id";
  return Slice();
}

void YQLRowwiseIteratorIf::SeekTuple(Slice tuple_id) {
  LOG(DFATAL) << "This iterator cannot seek by tuple id";
}

HybridTime YQLRowwiseIteratorIf::TEST_MaxSeenHt() {
  return HybridTime::kInvalid;
}

Result<bool> YQLRowwiseIteratorIf::FetchTuple(Slice tuple_id, qlexpr::QLTableRow* row) {
  return STATUS(NotSupported, "This iterator cannot fetch tuple id");
}

}  // namespace docdb
}  // namespace yb
