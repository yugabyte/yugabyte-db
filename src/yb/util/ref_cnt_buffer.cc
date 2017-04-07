//
// Copyright (c) YugaByte, Inc.
//

#include <glog/logging.h>

#include "yb/util/ref_cnt_buffer.h"

#include "yb/util/faststring.h"

namespace yb {
namespace util {

RefCntBuffer::RefCntBuffer()
    : data_(nullptr) {
}

RefCntBuffer::RefCntBuffer(size_t size)
    : data_(static_cast<char*>(malloc(size + sizeof(CounterType) + sizeof(size_t)))) {
  CHECK(data_ != nullptr);
  size_reference() = size;
  new (&counter_reference()) CounterType(1);
}

RefCntBuffer::RefCntBuffer(const char *data, size_t size)
    : data_(static_cast<char *>(malloc(size + sizeof(CounterType) + sizeof(size_t)))) {
  CHECK(data_ != nullptr);
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

} // namespace util
} // namespace yb
