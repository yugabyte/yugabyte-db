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

#pragma once

#include <stdint.h>

#include <sstream>
#include <string>
#include <vector>

#include <glog/logging.h>

namespace yb {

// Utility method to URL-encode a string (that is, replace special
// characters with %<hex value in ascii>).
// The optional parameter hive_compat controls whether we mimic Hive's
// behaviour when encoding a string, which is only to encode certain
// characters (excluding, e.g., ' ')
void UrlEncode(const std::string& in, std::string* out, bool hive_compat = false);
void UrlEncode(const std::vector<uint8_t>& in, std::string* out,
    bool hive_compat = false);
std::string UrlEncodeToString(const std::string& in, bool hive_compat = false);

// Utility method to decode a string that was URL-encoded. Returns
// true unless the string could not be correctly decoded.
// The optional parameter hive_compat controls whether or not we treat
// the strings as encoded by Hive, which means selectively ignoring
// certain characters like ' '.
bool UrlDecode(const std::string& in, std::string* out, bool hive_compat = false);

// Utility method to encode input as base-64 encoded.  This is not
// very performant (multiple string copies) and should not be used
// in a hot path.
void Base64Encode(const std::vector<uint8_t>& in, std::string* out);
void Base64Encode(const std::vector<uint8_t>& in, std::stringstream* out);
void Base64Encode(const std::string& in, std::string* out);
void Base64Encode(const std::string& in, std::stringstream* out);

// Utility method to decode base64 encoded strings.  Also not extremely
// performant.
// Returns true unless the string could not be correctly decoded.
bool Base64Decode(const std::string& in, std::string* out);

// Replaces &, <, > , " with &amp;, &lt;, &gt;, and &quot; respectively. This is
// not the full set of required encodings, but one that should be
// added to on a case-by-case basis. Slow, since it necessarily
// inspects each character in turn, and copies them all to *out; use
// judiciously.
template<class Stream>
void EscapeForHtml(Stream* in, std::stringstream* out) {
  DCHECK(out != nullptr);
  for (auto itr = std::istreambuf_iterator<typename Stream::char_type>(*in);
       itr != std::istreambuf_iterator<typename Stream::char_type>();
       ++itr) {
    switch (*itr) {
      case '<': (*out) << "&lt;";
                break;
      case '>': (*out) << "&gt;";
                break;
      case '&': (*out) << "&amp;";
                break;
      case '"': (*out) << "&quot;";
                break;
      default: (*out) << (*itr);
    }
  }
}

void EscapeForHtml(const std::string& in, std::stringstream* out);

// Same as above, but returns a string.
std::string EscapeForHtmlToString(const std::string& in);

} // namespace yb
