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

#include "yb/gutil/stringprintf.h"
#include "yb/util/status.h"

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
  return std::string(reinterpret_cast<const char *>(data_), size_);
}

std::string Slice::ToString(bool hex) const {
  return hex ? ToDebugHexString() : ToString();
}

std::string Slice::ToDebugHexString() const {
  std::string result;
  char buf[10];
  for (size_t i = 0; i < size_; i++) {
    snprintf(buf, sizeof(buf), "%02X", data_[i]);
    result += buf;
  }
  return result;
}

std::string Slice::ToDebugString(size_t max_len) const {
  size_t bytes_to_print = size_;
  bool abbreviated = false;
  if (max_len != 0 && bytes_to_print > max_len) {
    bytes_to_print = max_len;
    abbreviated = true;
  }

  int size = 0;
  for (int i = 0; i < bytes_to_print; i++) {
    if (!isgraph(data_[i])) {
      size += 4;
    } else {
      size++;
    }
  }
  if (abbreviated) {
    size += 20;  // extra padding
  }

  std::string ret;
  ret.reserve(size);
  for (int i = 0; i < bytes_to_print; i++) {
    if (!isgraph(data_[i])) {
      if (data_[i] == '\r') {
        ret += "\\r";
      } else if (data_[i] == '\n') {
        ret += "\\n";
      } else if (data_[i] == ' ') {
        ret += ' ';
      } else {
        StringAppendF(&ret, "\\x%02x", data_[i] & 0xff);
      }
    } else {
      ret.push_back(data_[i]);
    }
  }
  if (abbreviated) {
    StringAppendF(&ret, "...<%zd bytes total>", size_);
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
  data_ = reinterpret_cast<const uint8_t*>(buf->data());
  size_ = buf->size();
}

}  // namespace yb
