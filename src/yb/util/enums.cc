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

#include "yb/util/enums.h"

#include "yb/util/logging.h"

namespace yb {

[[noreturn]] void FatalInvalidEnumValueInternal(
    const char* enum_name,
    const std::string& full_enum_name,
    const std::string& value_str,
    int64_t value,
    const char* expression_str,
    const char* fname,
    int line) {
  google::LogMessageFatal(fname, line).stream()
      << "Invalid value of enum " << enum_name << " ("
      << "full enum type: " << full_enum_name << ", "
      << "expression: " << expression_str << "): "
      << value_str << (!value_str.empty() ? " (" : "")
      << value << (!value_str.empty() ? ")" : "") << ".";
  abort();  // Never reached.
}

}  // namespace yb
