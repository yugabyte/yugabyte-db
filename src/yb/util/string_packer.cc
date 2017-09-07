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

#include "yb/util/string_packer.h"
#include "yb/gutil/stringprintf.h"
#include "slice.h"

using std::string;
using std::vector;

namespace yb {
namespace util {

string PackZeroEncoded(const vector<string>& pieces) {
  string result;
  for (const string& piece : pieces) {
    for (char c : piece) {
      if (PREDICT_TRUE(c != '\x00')) {
        result.push_back(c);
      } else {
        result.push_back('\x00');
        result.push_back('\x01');
      }
    }
    result.push_back('\x00');
    result.push_back('\x00');
  }
  return result;
}

/**
 * Assume packed is correctly encoded. If not, behavior is unspecified.
 */
vector<string> UnpackZeroEncoded(string packed) {
  vector<string> result;
  if (packed.size() == 0)
    return result;
  result.push_back("");
  for (int i = 0; i < packed.size(); i++) {
    if (PREDICT_TRUE(packed[i] != '\x00')) {
      result[result.size() - 1].push_back(packed[i]);
    } else {
      i++;
      if (packed[i] == '\x01') {
        result[result.size() - 1].push_back('\x00');
      } else if (packed[i] == '\x00' && i < packed.size()-1) {
        result.push_back("");
      }
    }
  }
  return result;
}

}
}
