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
#include "yb/util/slice.h"

#include "yb/util/status.h"
#include "yb/util/status_format.h"

DEFINE_int32(non_graph_characters_percentage_to_use_hexadecimal_rendering, 10,
             "Non graph charaters percentage to use hexadecimal rendering");

namespace yb {

Status Slice::check_size(size_t expected_size) const {
  if (PREDICT_FALSE(size() != expected_size)) {
    return STATUS(Corruption, StringPrintf("Unexpected Slice size. "
        "Expected %zu but got %zu.", expected_size, size()), ToDebugString(100));
  }
  return Status::OK();
}

void Slice::CopyToBuffer(std::string* buffer) const {
  buffer->assign(cdata(), size());
}

// Return a string that contains the copy of the referenced data.
std::string Slice::ToBuffer() const {
  return std::string(cdata(), size());
}

std::string Slice::ToString(bool hex) const {
  return hex ? ToDebugHexString() : ToString();
}

std::string Slice::ToDebugHexString() const {
  std::string result;
  char buf[10];
  for (auto i = begin_; i != end_; ++i) {
    snprintf(buf, sizeof(buf), "%02X", *i);
    result += buf;
  }
  return result;
}

std::string Slice::ToDebugString(size_t max_len) const {
  size_t bytes_to_print = size();
  bool abbreviated = false;
  if (max_len != 0 && bytes_to_print > max_len) {
    bytes_to_print = max_len;
    abbreviated = true;
  }

  int num_not_graph = 0;
  for (size_t i = 0; i < bytes_to_print; i++) {
    if (!isgraph(begin_[i])) {
      ++num_not_graph;
    }
  }

  if (num_not_graph * 100 >
      bytes_to_print * FLAGS_non_graph_characters_percentage_to_use_hexadecimal_rendering) {
    return ToDebugHexString();
  }
  size_t size = bytes_to_print + 3 * num_not_graph + (abbreviated ? 20 : 0);

  std::string ret;
  ret.reserve(size);
  for (int i = 0; i < bytes_to_print; i++) {
    auto ch = begin_[i];
    if (!isgraph(ch)) {
      if (ch == '\r') {
        ret += "\\r";
      } else if (ch == '\n') {
        ret += "\\n";
      } else if (ch == ' ') {
        ret += ' ';
      } else {
        StringAppendF(&ret, "\\x%02x", ch & 0xff);
      }
    } else {
      ret.push_back(ch);
    }
  }
  if (abbreviated) {
    StringAppendF(&ret, "...<%zd bytes total>", this->size());
  }
  return ret;
}

Slice::Slice(const SliceParts& parts, std::string* buf) {
  size_t length = 0;
  for (int i = 0; i < parts.num_parts; ++i) {
    length += parts.parts[i].size();
  }
  buf->reserve(length);

  for (int i = 0; i < parts.num_parts; ++i) {
    buf->append(parts.parts[i].cdata(), parts.parts[i].size());
  }
  *this = Slice(*buf);
}

Status Slice::consume_byte(char c) {
  char consumed = consume_byte();
  if (consumed != c) {
    return STATUS_FORMAT(Corruption, "Wrong first byte, expected $0 but found $1",
                         static_cast<int>(c), static_cast<int>(consumed));
  }

  return Status::OK();
}

uint8_t Slice::operator[](size_t n) const {
  DCHECK_LT(n, size());
  return begin_[n];
}

void Slice::remove_prefix(size_t n) {
  DCHECK_LE(n, size());
  begin_ += n;
}

Slice Slice::Prefix(size_t n) const {
  DCHECK_LE(n, size());
  return Slice(begin_, n);
}

Slice Slice::WithoutPrefix(size_t n) const {
  DCHECK_LE(n, size());
  return Slice(begin_ + n, end_);
}

void Slice::remove_suffix(size_t n) {
  DCHECK_LE(n, size());
  end_ -= n;
}

Slice Slice::Suffix(size_t n) const {
  DCHECK_LE(n, size());
  return Slice(end_ - n, end_);
}

Slice Slice::WithoutSuffix(size_t n) const {
  DCHECK_LE(n, size());
  return Slice(begin_, end_ - n);
}

void Slice::truncate(size_t n) {
  DCHECK_LE(n, size());
  end_ = begin_ + n;
}

char Slice::consume_byte() {
  DCHECK_GT(end_, begin_);
  return *begin_++;
}

std::string SliceParts::ToDebugHexString() const {
  std::string result;
  for (int i = 0; i != num_parts; ++i) {
    result += parts[i].ToDebugHexString();
  }
  return result;
}

size_t SliceParts::SumSizes() const {
  size_t result = 0;
  for (int i = 0; i != num_parts; ++i) {
    result += parts[i].size();
  }
  return result;
}

void SliceParts::CopyAllTo(void* out) const {
  char* buf = static_cast<char*>(out);
  for (int i = 0; i != num_parts; ++i) {
    if (!parts[i].size()) {
      continue;
    }
    memcpy(buf, parts[i].data(), parts[i].size());
    buf += parts[i].size();
  }
}

}  // namespace yb
