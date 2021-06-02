//
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
//

#include <glog/logging.h>

#include "yb/util/ref_cnt_buffer.h"

#include "yb/util/faststring.h"
#include "yb/util/malloc.h"

namespace yb {

RefCntBuffer::RefCntBuffer()
    : data_(nullptr) {
}

size_t RefCntBuffer::GetInternalBufSize(size_t data_size) {
  return data_size + sizeof(CounterType) + sizeof(size_t);
}

RefCntBuffer::RefCntBuffer(size_t size) {
  data_ = malloc_with_check(GetInternalBufSize(size));
  size_reference() = size;
  new (&counter_reference()) CounterType(1);
}

RefCntBuffer::RefCntBuffer(const char* data, size_t size) {
  data_ = malloc_with_check(GetInternalBufSize(size));
  memcpy(this->data(), data, size);
  size_reference() = size;
  new (&counter_reference()) CounterType(1);
}

RefCntBuffer::RefCntBuffer(const faststring& string)
    : RefCntBuffer(string.data(), string.size()) {
}

RefCntBuffer::~RefCntBuffer() {
  Reset();
}

RefCntBuffer::RefCntBuffer(const RefCntBuffer& rhs) noexcept
    : data_(rhs.data_) {
  if (data_)
    ++counter_reference();
}

RefCntBuffer::RefCntBuffer(RefCntBuffer&& rhs) noexcept
    : data_(rhs.data_) {
  rhs.data_ = nullptr;
}

void RefCntBuffer::operator=(const RefCntBuffer& rhs) noexcept {
  if (rhs.data_) {
    ++rhs.counter_reference();
  }
  DoReset(rhs.data_);
}

void RefCntBuffer::operator=(RefCntBuffer&& rhs) noexcept {
  DoReset(rhs.data_);
  rhs.data_ = nullptr;
}

void RefCntBuffer::DoReset(char* data) {
  if (data_ != nullptr) {
    if (--counter_reference() == 0) {
      counter_reference().~CounterType();
      free(data_);
    }
  }
  data_ = data;
}

std::string RefCntPrefix::ShortDebugString() const {
  return Slice(data(), size()).ToDebugHexString();
}

} // namespace yb
