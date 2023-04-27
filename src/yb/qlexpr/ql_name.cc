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

#include "yb/qlexpr/ql_name.h"
#include <boost/algorithm/string/replace.hpp>

using std::string;

namespace yb::qlexpr {

// Prefix for various kinds of identifiers.
// Currently we only care about table::columns and jsonb::attributes.
static const char* kYcqlColumnPrefix = "C$_";                                           // columns.
static const char* kYcqlJattrPrefix = "J$_";                                    // JSONB attribute.
static const char* kYcqlEscape = "$$";                                         // Escape character.
static const char* kYcqlUnescape = "$";                                        // Escape character.

string YcqlName::MangleColumnName(const string& col_name) {
  // Add prefix for column and escaping by replacing all appearances of '$' with '$$'.
  return kYcqlColumnPrefix + ReplacePattern(col_name, kYcqlUnescape, kYcqlEscape);
}

string YcqlName::MangleJsonAttrName(const string& attr_name) {
  // Add prefix for JSONB attribute and escaping by replacing all appearances of '$' with '$$'.
  return kYcqlJattrPrefix + ReplacePattern(attr_name, kYcqlUnescape, kYcqlEscape);
}

string YcqlName::DemangleName(const string& col_name) {
  // Removing all prefixes and un-escaping by replacing all appearances of '$$' with '$'.
  string name;
  name = ReplacePattern(col_name, kYcqlColumnPrefix, "");
  name = ReplacePattern(name, kYcqlJattrPrefix, "");
  name = ReplacePattern(name, kYcqlEscape, kYcqlUnescape);
  return name;
}

string YcqlName::ReplacePattern(const string& ql_name, const string& pattern, const string& str) {
  string name = ql_name;
  boost::replace_all(name, pattern, str);
  return name;
}

}  // namespace yb::qlexpr
