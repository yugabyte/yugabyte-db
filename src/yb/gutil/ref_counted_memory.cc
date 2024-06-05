// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
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

#include "yb/gutil/ref_counted_memory.h"

#include "yb/util/logging.h"

namespace yb {

bool RefCountedMemory::Equals(
    const scoped_refptr<RefCountedMemory>& other) const {
  return other.get() &&
         size() == other->size() &&
         (memcmp(front(), other->front(), size()) == 0);
}

RefCountedMemory::RefCountedMemory() {}

RefCountedMemory::~RefCountedMemory() {}

const unsigned char* RefCountedStaticMemory::front() const {
  return data_;
}

size_t RefCountedStaticMemory::size() const {
  return length_;
}

RefCountedStaticMemory::~RefCountedStaticMemory() {}

RefCountedBytes::RefCountedBytes() {}

RefCountedBytes::RefCountedBytes(std::vector<unsigned char> initializer)
    : data_(std::move(initializer)) {}

RefCountedBytes::RefCountedBytes(const unsigned char* p, size_t size)
    : data_(p, p + size) {}

RefCountedBytes* RefCountedBytes::TakeVector(
    std::vector<unsigned char>* to_destroy) {
  auto bytes = new RefCountedBytes;
  bytes->data_.swap(*to_destroy);
  return bytes;
}

const unsigned char* RefCountedBytes::front() const {
  // STL will assert if we do front() on an empty vector, but calling code
  // expects a NULL.
  return size() ? &data_.front() : nullptr;
}

size_t RefCountedBytes::size() const {
  return data_.size();
}

RefCountedBytes::~RefCountedBytes() {}

RefCountedString::RefCountedString() {}

RefCountedString::~RefCountedString() {}

// static
RefCountedString* RefCountedString::TakeString(std::string* to_destroy) {
  auto self = new RefCountedString;
  to_destroy->swap(self->data_);
  return self;
}

const unsigned char* RefCountedString::front() const {
  return data_.empty() ? nullptr :
         reinterpret_cast<const unsigned char*>(data_.data());
}

size_t RefCountedString::size() const {
  return data_.size();
}

RefCountedMallocedMemory::RefCountedMallocedMemory(
    void* data, size_t length)
    : data_(reinterpret_cast<unsigned char*>(data)), length_(length) {
  DCHECK(data || length == 0);
}

const unsigned char* RefCountedMallocedMemory::front() const {
  return length_ ? data_ : nullptr;
}

size_t RefCountedMallocedMemory::size() const {
  return length_;
}

RefCountedMallocedMemory::~RefCountedMallocedMemory() {
  free(data_);
}

}  //  namespace yb
