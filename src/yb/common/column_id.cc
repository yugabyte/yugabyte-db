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
#include "yb/common/column_id.h"

#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

namespace yb {

ColumnId::ColumnId(ColumnIdRep t) : t_(t) {
  DCHECK_GE(t, 0);
}

ColumnId& ColumnId::operator=(const ColumnIdRep& rhs) {
  DCHECK_GE(rhs, 0);
  t_ = rhs;
  return *this;
}

Status ColumnId::FromInt64(int64_t value, ColumnId *column_id) {
  if (value > std::numeric_limits<ColumnIdRep>::max() || value < 0) {
    return STATUS_FORMAT(Corruption, "$0 not valid for column id representation", value);
  }
  column_id->t_ = static_cast<ColumnIdRep>(value);
  return Status::OK();
}

uint64_t ColumnId::ToUint64() const {
  DCHECK_GE(t_, 0);
  return static_cast<uint64_t>(t_);
}

std::ostream& operator<<(std::ostream& os, ColumnId column_id) {
  return os << column_id.rep();
}

}  // namespace yb
