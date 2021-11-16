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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/util/logging.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>
#include <stdio.h>
#include "yb/util/slice.h"

namespace rocksdb {

// for micros < 10ms, print "XX us".
// for micros < 10sec, print "XX ms".
// for micros >= 10 sec, print "XX sec".
// for micros <= 1 hour, print Y:X M:S".
// for micros > 1 hour, print Z:Y:X H:M:S".
int AppendHumanMicros(uint64_t micros, char* output, int len,
                      bool fixed_format) {
  if (micros < 10000 && !fixed_format) {
    return snprintf(output, len, "%" PRIu64 " us", micros);
  } else if (micros < 10000000 && !fixed_format) {
    return snprintf(output, len, "%.3lf ms",
                    static_cast<double>(micros) / 1000);
  } else if (micros < 1000000l * 60 && !fixed_format) {
    return snprintf(output, len, "%.3lf sec",
                    static_cast<double>(micros) / 1000000);
  } else if (micros < 1000000ll * 60 * 60 && !fixed_format) {
    return snprintf(output, len, "%02" PRIu64 ":%05.3f M:S",
        micros / 1000000 / 60,
        static_cast<double>(micros % 60000000) / 1000000);
  } else {
    return snprintf(output, len,
        "%02" PRIu64 ":%02" PRIu64 ":%05.3f H:M:S",
        micros / 1000000 / 3600,
        (micros / 1000000 / 60) % 60,
        static_cast<double>(micros % 60000000) / 1000000);
  }
}

// for sizes >=10TB, print "XXTB"
// for sizes >=10GB, print "XXGB"
// etc.
// append file size summary to output and return the len
int AppendHumanBytes(uint64_t bytes, char* output, int len) {
  const uint64_t ull10 = 10;
  if (bytes >= ull10 << 40) {
    return snprintf(output, len, "%" PRIu64 "TB", bytes >> 40);
  } else if (bytes >= ull10 << 30) {
    return snprintf(output, len, "%" PRIu64 "GB", bytes >> 30);
  } else if (bytes >= ull10 << 20) {
    return snprintf(output, len, "%" PRIu64 "MB", bytes >> 20);
  } else if (bytes >= ull10 << 10) {
    return snprintf(output, len, "%" PRIu64 "KB", bytes >> 10);
  } else {
    return snprintf(output, len, "%" PRIu64 "B", bytes);
  }
}

void AppendNumberTo(std::string* str, uint64_t num) {
  char buf[30];
  snprintf(buf, sizeof(buf), "%" PRIu64, num);
  str->append(buf);
}

void AppendBoolTo(std::string* str, const bool b) {
  str->append(b ? "true" : "false");
}

void AppendEscapedStringTo(std::string* str, const Slice& value) {
  for (size_t i = 0; i < value.size(); i++) {
    char c = value[i];
    if (c >= ' ' && c <= '~') {
      str->push_back(c);
    } else {
      char buf[10];
      snprintf(buf, sizeof(buf), "\\x%02x",
               static_cast<unsigned int>(c) & 0xff);
      str->append(buf);
    }
  }
}

std::string NumberToString(uint64_t num) {
  std::string r;
  AppendNumberTo(&r, num);
  return r;
}

std::string NumberToHumanString(int64_t num) {
  char buf[24];
  int64_t absnum = num < 0 ? -num : num;
  if (absnum < 10000) {
    snprintf(buf, sizeof(buf), "%" PRIi64, num);
  } else if (absnum < 10000000) {
    snprintf(buf, sizeof(buf), "%" PRIi64 "K", num / 1000);
  } else if (absnum < 10000000000LL) {
    snprintf(buf, sizeof(buf), "%" PRIi64 "M", num / 1000000);
  } else {
    snprintf(buf, sizeof(buf), "%" PRIi64 "G", num / 1000000000);
  }
  return std::string(buf);
}

std::string EscapeString(const Slice& value) {
  std::string r;
  AppendEscapedStringTo(&r, value);
  return r;
}

bool ConsumeDecimalNumber(Slice* in, uint64_t* val) {
  uint64_t v = 0;
  int digits = 0;
  while (!in->empty()) {
    char c = (*in)[0];
    if (c >= '0' && c <= '9') {
      ++digits;
      const unsigned int delta = (c - '0');
      static const uint64_t kMaxUint64 = ~static_cast<uint64_t>(0);
      if (v > kMaxUint64/10 ||
          (v == kMaxUint64/10 && delta > kMaxUint64%10)) {
        // Overflow
        return false;
      }
      v = (v * 10) + delta;
      in->remove_prefix(1);
    } else {
      break;
    }
  }
  *val = v;
  return (digits > 0);
}

}  // namespace rocksdb
