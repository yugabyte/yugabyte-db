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

#include "yb/yql/cql/ql/ptree/column_desc.h"

#include "yb/qlexpr/ql_name.h"

namespace yb {
namespace ql {

std::string ColumnDesc::name() const {
  // Demangle "name_" if it was previous mangled (IndexTable has mangled column names).
  if (has_mangled_name_) {
    return qlexpr::YcqlName::DemangleName(name_);
  }
  return name_;
}

std::string ColumnDesc::MangledName() const {
  // Mangle "name_" if it was not previous mangled.
  // When we load INDEX, the column name is already mangled (Except for older INDEX).
  if (has_mangled_name_) {
    return name_;
  }
  return qlexpr::YcqlName::MangleColumnName(name_);
}

}  // namespace ql
}  // namespace yb
