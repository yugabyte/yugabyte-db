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

#include <algorithm>
#include <string>

#include "kudu/gutil/stringprintf.h"
#include "kudu/util/hexdump.h"
#include "kudu/util/slice.h"

namespace kudu {

std::string HexDump(const Slice &slice) {
  std::string output;
  output.reserve(slice.size() * 5);

  const uint8_t *p = slice.data();

  int rem = slice.size();
  while (rem > 0) {
    const uint8_t *line_p = p;
    int line_len = std::min(rem, 16);
    int line_rem = line_len;
    StringAppendF(&output, "%06lx: ", line_p - slice.data());

    while (line_rem >= 2) {
      StringAppendF(&output, "%02x%02x ",
                    p[0] & 0xff, p[1] & 0xff);
      p += 2;
      line_rem -= 2;
    }

    if (line_rem == 1) {
      StringAppendF(&output, "%02x   ",
                    p[0] & 0xff);
      p += 1;
      line_rem -= 1;
    }

    int padding = (16 - line_len) / 2;

    for (int i = 0; i < padding; i++) {
      output.append("     ");
    }

    for (int i = 0; i < line_len; i++) {
      char c = line_p[i];
      if (isprint(c)) {
        output.push_back(c);
      } else {
        output.push_back('.');
      }
    }

    output.push_back('\n');
    rem -= line_len;
  }
  return output;
}
} // namespace kudu
