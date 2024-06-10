//
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

#include "yb/common/row_mark.h"

#include "yb/util/logging.h"

#include "yb/common/common.pb.h"
#include "yb/gutil/macros.h"

namespace yb {

RowMarkType GetStrongestRowMarkType(std::initializer_list<RowMarkType> row_mark_types) {
  RowMarkType strongest_row_mark_type = RowMarkType::ROW_MARK_ABSENT;
  for (RowMarkType row_mark_type : row_mark_types) {
    strongest_row_mark_type = std::min(row_mark_type, strongest_row_mark_type);
  }
  return strongest_row_mark_type;
}

bool IsValidRowMarkType(RowMarkType row_mark_type) {
  switch (row_mark_type) {
    case RowMarkType::ROW_MARK_EXCLUSIVE: FALLTHROUGH_INTENDED;
    case RowMarkType::ROW_MARK_NOKEYEXCLUSIVE: FALLTHROUGH_INTENDED;
    case RowMarkType::ROW_MARK_SHARE: FALLTHROUGH_INTENDED;
    case RowMarkType::ROW_MARK_KEYSHARE:
      return true;
    default:
      return false;
  }
}

bool RowMarkNeedsHigherPriority(RowMarkType row_mark_type) {
  /*
   * Currently, using higher priority for all supported row marks except the key share lock.
   * This is because key share locks are used for foreign keys and we don't want higher priority
   * there.
   */
  return IsValidRowMarkType(row_mark_type) &&
      row_mark_type != RowMarkType::ROW_MARK_KEYSHARE;
}

} // namespace yb
