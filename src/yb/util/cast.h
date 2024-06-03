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

#pragma once

#include <memory>

namespace yb {

inline char* to_char_ptr(uint8_t* uptr) {
  return reinterpret_cast<char *>(uptr);
}

inline const char* to_char_ptr(const uint8_t* uptr) {
  return reinterpret_cast<const char *>(uptr);
}

inline uint8_t* to_uchar_ptr(char *ptr) {
  return reinterpret_cast<uint8_t *>(ptr);
}

inline const uint8_t* to_uchar_ptr(const char *ptr) {
  return reinterpret_cast<const uint8_t *>(ptr);
}

inline uint8_t* to_uchar_ptr(std::byte *ptr) {
  return reinterpret_cast<uint8_t *>(ptr);
}

inline const uint8_t* to_uchar_ptr(const std::byte *ptr) {
  return reinterpret_cast<const uint8_t *>(ptr);
}

template<class Out, class In>
Out pointer_cast(In* in) {
  void* temp = in;
  return static_cast<Out>(temp);
}

template<class Out, class In>
Out pointer_cast(const In* in) {
  const void* temp = in;
  return static_cast<Out>(temp);
}

template<class D, class S>
std::unique_ptr<D> down_pointer_cast(std::unique_ptr<S> s) {
  auto* f = s.release();
  assert(f == nullptr || dynamic_cast<D*>(f) != nullptr);
  return std::unique_ptr<D>(static_cast<D*>(f));
}

}  // namespace yb

using yb::pointer_cast;
