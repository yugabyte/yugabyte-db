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
//
// This file contains the QLValue class that represents QL values.

#pragma once

#include <string>

namespace yb::qlexpr {

enum class QLNameOption : int8_t {
  // Read names that were enterred by users.
  kUserOriginalName,

  // Read mangled names.
  kMangledName,

  // Read the names that were loaded from sytem catalog.
  // - Catalog::Table::Column has original names.
  // - Catalong::IndexTable::ExprColumn has mangled name.
  kMetadataName
};

// This class handles mangling and demangling database object names. Currently, we only need to do
// so for columns and JSONB attributes.
class YcqlName {
 public:
  static std::string MangleColumnName(const std::string& col_name);
  static std::string MangleJsonAttrName(const std::string& attr_name);
  static std::string DemangleName(const std::string& col_name);

 private:
  static std::string ReplacePattern(const std::string& ql_name,
                                    const std::string& pattern,
                                    const std::string& str);
};

}  // namespace yb::qlexpr
