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
// Macros for dealing with memory alignment.
#pragma once

#include <cstddef>

namespace yb {

// Round down 'x' to the nearest 'align' boundary
#define YB_ALIGN_DOWN(x, align) ((x) & (-(align)))

template<class T>
class AlignmentTraits {
 public:
  typedef T value_type;

  static value_type to_int(T x) {
    return x;
  }

  static T from_int(value_type value) {
    return value;
  }
};

template<class T>
class AlignmentTraits<T*> {
 public:
  typedef size_t value_type;

  static value_type to_int(T* ptr) {
    return reinterpret_cast<value_type>(ptr);
  }

  static T* from_int(value_type value) {
    return reinterpret_cast<T*>(value);
  }
};

// Round up 'ptr' to the nearest 'align' boundary.
// T should be pointer or integer type.
template<class T>
T align_up(T ptr, typename AlignmentTraits<T>::value_type alignment_bytes) {
  typedef AlignmentTraits<T> Traits;
  typedef typename Traits::value_type int_type;
  static_assert(sizeof(T) == sizeof(int_type), "Invalid source size in align_up");
  auto x = Traits::to_int(ptr);
  return Traits::from_int((x + alignment_bytes - 1) & -alignment_bytes);
}

} // namespace yb
