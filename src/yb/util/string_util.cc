//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/util/string_util.h"

#include <sstream>

using std::vector;
using std::string;
using std::stringstream;

namespace yb {

vector<string> StringSplit(const string& arg, char delim) {
  vector<string> splits;
  stringstream ss(arg);
  string item;
  while (getline(ss, item, delim)) {
    splits.push_back(item);
  }
  return splits;
}

std::string RightPadToWidth(const string& s, int w) {
  int padding = w - s.size();
  if (padding <= 0)
    return s;
  return s + string(padding, ' ');
}

bool StringEndsWith(const string& s, const char* end, size_t end_len, string* left) {
  // For our purpose, s should always have at least one character before the string we are looking
  // for.
  if (s.length() <= end_len) {
    return false;
  }
  if (s.find(end, s.length() - end_len) != string::npos) {
    if (left != nullptr) {
      *left = s.substr(0, s.length() - end_len);
    }
    return true;
  }
  return false;
}

}  // namespace yb
