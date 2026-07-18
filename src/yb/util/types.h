// Copyright (c) YugabyteDB, Inc.
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

#include <cstddef>

#include "yb/util/cast.h"

namespace yb {

template<typename Container>
Container* MemberPtrToContainer(void* member_ptr, size_t member_offset) {
  return pointer_cast<Container*>(pointer_cast<std::byte*>(member_ptr) - member_offset);
}

template<typename Container>
const Container* MemberPtrToContainer(const void* member_ptr, size_t member_offset) {
  return pointer_cast<const Container*>(pointer_cast<const std::byte*>(member_ptr) - member_offset);
}

#define MEMBER_PTR_TO_CONTAINER(cls, member_name, member_ptr) \
    ::yb::MemberPtrToContainer<cls>(member_ptr, offsetof(cls, member_name))

} // namespace yb
