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

#ifndef YB_UTIL_REF_CNT_BUFFER_H
#define YB_UTIL_REF_CNT_BUFFER_H

#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <string>

namespace yb {

class faststring;

// Byte buffer with reference counting. It embeds reference count, size and data in a single block.
class RefCntBuffer {
 public:
  RefCntBuffer();
  explicit RefCntBuffer(size_t size);
  RefCntBuffer(const char *data, size_t size);

  RefCntBuffer(const char *data, const char *end)
      : RefCntBuffer(data, end - data) {}

  RefCntBuffer(const uint8_t *data, size_t size)
      : RefCntBuffer(static_cast<const char*>(static_cast<const void*>(data)), size) {}

  explicit RefCntBuffer(const std::string& string) :
      RefCntBuffer(string.c_str(), string.length()) {}

  explicit RefCntBuffer(const faststring& string);

  RefCntBuffer(const RefCntBuffer& rhs) noexcept;
  RefCntBuffer(RefCntBuffer&& rhs) noexcept;

  void operator=(const RefCntBuffer& rhs) noexcept;
  void operator=(RefCntBuffer&& rhs) noexcept;

  ~RefCntBuffer();

  size_t size() const {
    return size_reference();
  }

  bool empty() const {
    return size() == 0;
  }

  char* data() const {
    return data_ + sizeof(CounterType) + sizeof(size_t);
  }

  char* begin() const {
    return data();
  }

  char* end() const {
    return begin() + size();
  }

  uint8_t* udata() const {
    return static_cast<unsigned char*>(static_cast<void*>(data()));
  }

  uint8_t* ubegin() const {
    return udata();
  }

  uint8_t* uend() const {
    return udata() + size();
  }

  void Reset() { DoReset(nullptr); }

  explicit operator bool() const {
    return data_ != nullptr;
  }

  bool operator!() const {
    return data_ == nullptr;
  }

  std::string ToBuffer() const {
    return std::string(begin(), end());
  }

 private:
  void DoReset(char* data);

  // Using ptrdiff_t since it matches register size and is signed.
  typedef std::atomic<std::ptrdiff_t> CounterType;

  size_t& size_reference() const {
    return *static_cast<size_t*>(static_cast<void*>(data_ + sizeof(CounterType)));
  }

  CounterType& counter_reference() const {
    return *static_cast<CounterType*>(static_cast<void*>(data_));
  }

  char *data_;
};

} // namespace yb

#endif // YB_UTIL_REF_CNT_BUFFER_H
