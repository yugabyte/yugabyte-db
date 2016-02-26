// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// Utility methods for dealing with string case.
#ifndef KUDU_UTIL_STRING_CASE_H
#define KUDU_UTIL_STRING_CASE_H

#include <string>

namespace kudu {

// Convert the given snake_case string to camel case.
// Also treats '-' in a string like a '_'
// For example:
// - 'foo_bar' -> FooBar
// - 'foo-bar' -> FooBar
//
// This function cannot operate in-place -- i.e. 'camel_case' must not
// point to 'snake_case'.
void SnakeToCamelCase(const std::string &snake_case,
                      std::string *camel_case);

// Upper-case all of the characters in the given string.
// 'string' and 'out' may refer to the same string to replace in-place.
void ToUpperCase(const std::string &string,
                 std::string *out);

// Capitalizes a string containing a word in place.
// For example:
// - 'hiBerNATe' -> 'Hibernate'
void Capitalize(std::string *word);

} // namespace kudu
#endif
