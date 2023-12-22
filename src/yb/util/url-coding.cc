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
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "yb/util/url-coding.h"

#include <boost/algorithm/string.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>

#include <glog/logging.h>

using std::string;
using std::vector;
using namespace boost::archive::iterators; // NOLINT(*)

namespace yb {

namespace {

// Hive selectively encodes characters. This is the whitelist of
// characters it will encode.
// See common/src/java/org/apache/hadoop/hive/common/FileUtils.java
// in the Hive source code for the source of this list.
std::function<bool(char)> HiveShouldEscape =
    boost::is_any_of("\"#%\\*/:=?\u00FF"); // NOLINT(*)

// It is more convenient to maintain the complement of the set of
// characters to escape when not in Hive-compat mode.
std::function<bool(char)> ShouldNotEscape = boost::is_any_of("-_.~"); // NOLINT(*)

inline void UrlEncode(const char* in, size_t in_len, string* out, bool hive_compat) {
  (*out).reserve(in_len);
  std::stringstream ss;
  for (size_t i = 0; i < in_len; ++i) {
    const char ch = in[i];
    // Escape the character iff a) we are in Hive-compat mode and the
    // character is in the Hive whitelist or b) we are not in
    // Hive-compat mode, and the character is not alphanumeric or one
    // of the four commonly excluded characters.
    if ((hive_compat && HiveShouldEscape(ch)) ||
        (!hive_compat && !(isalnum(ch) || ShouldNotEscape(ch)))) {
      ss << '%' << std::uppercase << std::hex << static_cast<uint32_t>(ch);
    } else {
      ss << ch;
    }
  }

  (*out) = ss.str();
}

} // namespace

void UrlEncode(const vector<uint8_t>& in, string* out, bool hive_compat) {
  if (in.empty()) {
    *out = "";
  } else {
    UrlEncode(reinterpret_cast<const char*>(&in[0]), in.size(), out, hive_compat);
  }
}

void UrlEncode(const string& in, string* out, bool hive_compat) {
  UrlEncode(in.c_str(), in.size(), out, hive_compat);
}

string UrlEncodeToString(const std::string& in, bool hive_compat) {
  string ret;
  UrlEncode(in, &ret, hive_compat);
  return ret;
}

// Adapted from
// http://www.boost.org/doc/libs/1_40_0/doc/html/boost_asio/
//   example/http/server3/request_handler.cpp
// See http://www.boost.org/LICENSE_1_0.txt for license for this method.
bool UrlDecode(const string& in, string* out, bool hive_compat) {
  out->clear();
  out->reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '%') {
      if (i + 3 <= in.size()) {
        int value = 0;
        std::istringstream is(in.substr(i + 1, 2));
        if (is >> std::hex >> value) {
          (*out) += static_cast<char>(value);
          i += 2;
        } else {
          return false;
        }
      } else {
        return false;
      }
    } else if (!hive_compat && in[i] == '+') { // Hive does not encode ' ' as '+'
      (*out) += ' ';
    } else {
      (*out) += in[i];
    }
  }
  return true;
}

static inline void Base64Encode(const char* in, size_t in_len, std::stringstream* out) {
  typedef base64_from_binary<transform_width<const char*, 6, 8> > base64_encode;
  // Base64 encodes 8 byte chars as 6 bit values.
  std::stringstream::pos_type len_before = out->tellp();
  copy(base64_encode(in), base64_encode(in + in_len), std::ostream_iterator<char>(*out));
  auto bytes_written = out->tellp() - len_before;
  // Pad with = to make it valid base64 encoded string
  int num_pad = bytes_written % 4;
  if (num_pad != 0) {
    num_pad = 4 - num_pad;
    for (int i = 0; i < num_pad; ++i) {
      (*out) << "=";
    }
  }
  DCHECK_EQ(out->str().size() % 4, 0);
}

void Base64Encode(const vector<uint8_t>& in, string* out) {
  if (in.empty()) {
    *out = "";
  } else {
    std::stringstream ss;
    Base64Encode(in, &ss);
    *out = ss.str();
  }
}

void Base64Encode(const vector<uint8_t>& in, std::stringstream* out) {
  if (!in.empty()) {
    // Boost does not like non-null terminated strings
    string tmp(reinterpret_cast<const char*>(&in[0]), in.size());
    Base64Encode(tmp.c_str(), tmp.size(), out);
  }
}

void Base64Encode(const string& in, string* out) {
  std::stringstream ss;
  Base64Encode(in.c_str(), in.size(), &ss);
  *out = ss.str();
}

void Base64Encode(const string& in, std::stringstream* out) {
  Base64Encode(in.c_str(), in.size(), out);
}

bool Base64Decode(const string& in, string* out) {
  typedef transform_width<binary_from_base64<string::const_iterator>, 8, 6> base64_decode;
  string tmp = in;
  // Replace padding with base64 encoded NULL
  replace(tmp.begin(), tmp.end(), '=', 'A');
  try {
    *out = string(base64_decode(tmp.begin()), base64_decode(tmp.end()));
  } catch(std::exception& e) {
    return false;
  }

  // Remove trailing '\0' that were added as padding.  Since \0 is special,
  // the boost functions get confused so do this manually.
  int num_padded_chars = 0;
  for (size_t i = out->size(); i > 0;) {
    --i;
    if ((*out)[i] != '\0') break;
    ++num_padded_chars;
  }
  out->resize(out->size() - num_padded_chars);
  return true;
}

void EscapeForHtml(const string& in, std::stringstream* out) {
  auto ss = std::istringstream(in);
  EscapeForHtml(&ss, out);
}

std::string EscapeForHtmlToString(const std::string& in) {
  std::stringstream str;
  EscapeForHtml(in, &str);
  return str.str();
}

} // namespace yb
