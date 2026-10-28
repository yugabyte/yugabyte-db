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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
#include "yb/util/hexdump.h"

#include <string>

#include "yb/util/format.h"
#include "yb/util/slice.h"

namespace yb {

std::string HexDump(const Slice &slice) {
  std::string output;
  output.reserve(slice.size() * 5);

  const uint8_t *p = slice.data();

  auto rem = slice.size();
  while (rem > 0) {
    const uint8_t *line_p = p;
    auto line_len = std::min<decltype(rem)>(rem, 16);
    auto line_rem = line_len;
    output += Format("$0: ", HexString(line_p - slice.data(), 6));

    while (line_rem >= 2) {
      output += Format("$0$1 ", HexString(p[0], 2), HexString(p[1], 2));
      p += 2;
      line_rem -= 2;
    }

    if (line_rem == 1) {
      output += Format("$0   ", HexString(p[0], 2));
      p += 1;
      line_rem -= 1;
    }

    auto padding = (16 - line_len) / 2;

    for (size_t i = 0; i < padding; i++) {
      output.append("     ");
    }

    for (size_t i = 0; i < line_len; i++) {
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
} // namespace yb
