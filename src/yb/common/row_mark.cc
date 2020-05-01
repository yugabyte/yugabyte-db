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

#include <glog/logging.h>

#include "yb/common/common.pb.h"
#include "yb/gutil/macros.h"

namespace yb {

bool AreConflictingRowMarkTypes(
    const RowMarkType row_mark_type_a,
    const RowMarkType row_mark_type_b) {
  constexpr int kConflictThreshold = 4;
  const unsigned int value_a = static_cast<unsigned int>(row_mark_type_a);
  const unsigned int value_b = static_cast<unsigned int>(row_mark_type_b);

  // TODO: remove this when issue #2922 is fixed.
  if ((row_mark_type_a == RowMarkType::ROW_MARK_NOKEYEXCLUSIVE &&
       row_mark_type_b == RowMarkType::ROW_MARK_KEYSHARE) ||
      (row_mark_type_a == RowMarkType::ROW_MARK_KEYSHARE &&
       row_mark_type_b == RowMarkType::ROW_MARK_NOKEYEXCLUSIVE)) {
    return true;
  }

  return (value_a + value_b < kConflictThreshold);
}

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
      break;
    default:
      return false;
      break;
  }
}

bool RowMarkNeedsPessimisticLock(RowMarkType row_mark_type) {
  /*
   * Currently, using pessimistic locking for all supported row marks except the key share lock.
   * This is because key share locks are used for foreign keys and we don't want pessimistic
   * locking there.
   */
  return IsValidRowMarkType(row_mark_type) &&
      row_mark_type != RowMarkType::ROW_MARK_KEYSHARE;
}

std::string RowMarkTypeToPgsqlString(const RowMarkType row_mark_type) {
  switch (row_mark_type) {
    case RowMarkType::ROW_MARK_EXCLUSIVE:
      return "UPDATE";
      break;
    case RowMarkType::ROW_MARK_NOKEYEXCLUSIVE:
      return "NO KEY UPDATE";
      break;
    case RowMarkType::ROW_MARK_SHARE:
      return "SHARE";
      break;
    case RowMarkType::ROW_MARK_KEYSHARE:
      return "KEY SHARE";
      break;
    default:
      // We shouldn't get here because other row lock types are disabled at the postgres level.
      LOG(DFATAL) << "Unsupported row lock of type " << RowMarkType_Name(row_mark_type);
      return "";
      break;
  }
}

} // namespace yb
