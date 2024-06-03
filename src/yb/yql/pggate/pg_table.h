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

#include <memory>
#include <vector>

#include "yb/gutil/ref_counted.h"

#include "yb/util/result.h"

#include "yb/yql/pggate/pg_column.h"
#include "yb/yql/pggate/pg_gate_fwd.h"

namespace yb {
namespace pggate {

class PgTable {
 public:
  PgTable() = default;
  explicit PgTable(const PgTableDescPtr& desc);

  bool operator!() const {
    return !desc_;
  }

  explicit operator bool() const {
    return desc_ != nullptr;
  }

  PgTableDesc* operator->() const {
    return desc_.get();
  }

  PgTableDesc& operator*() const {
    return *desc_;
  }

  const std::vector<PgColumn>& columns() const {
    return *columns_;
  }

  std::vector<PgColumn>& columns() {
    return *columns_;
  }

  Result<PgColumn&> ColumnForAttr(int attr_num);
  [[nodiscard]] PgColumn& ColumnForIndex(size_t index);
  [[nodiscard]] const PgColumn& ColumnForIndex(size_t index) const;

 private:
  PgTableDescPtr desc_;
  std::shared_ptr<std::vector<PgColumn>> columns_;
};

}  // namespace pggate
}  // namespace yb
