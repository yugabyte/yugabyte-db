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

#include "yb/util/pg_quote.h"

#include <string>

namespace yb {

namespace {

// Taken from <https://stackoverflow.com/a/24315631> by Gauthier Boaglio.
std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
  size_t start_pos = 0;
  while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
    str.replace(start_pos, from.length(), to);
    start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
  }
  return str;
}

} // anonymous namespace

// Quote names in PG connection string values (e.g. to make a libpq connection to a database named
// `this->'\<-this`, use `dbname='this->\'\\<-this'`).
std::string QuotePgConnStrValue(const std::string input) {
  std::string output = input;
  // Escape certain characters.
  output = ReplaceAll(output, "\\", "\\\\");
  output = ReplaceAll(output, "'", "\\'");
  // Quote.
  output = "'" + output + "'";
  return output;
}

// Quote names in PG queries (e.g. to create a database named `this->"\<-this`, use `CREATE DATABASE
// "this->""\<-this"`).
std::string QuotePgName(const std::string input) {
  std::string output = input;
  // Escape certain characters.
  output = ReplaceAll(output, "\"", "\"\"");
  // Quote.
  output = "\"" + output + "\"";
  return output;
}

} // namespace yb
