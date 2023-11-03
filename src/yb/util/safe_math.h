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
// Inline functions for doing overflow-safe operations on integers.
// These should be used when doing bounds checks on user-provided data,
// for example.
// See also: https://www.securecoding.cert.org/confluence/display/cplusplus/INT32-CPP.+Ensure+that+operations+on+signed+integers+do+not+result+in+overflow
#pragma once

#include "yb/gutil/mathlimits.h"

namespace yb {

namespace safe_math_internal {

// Template which is specialized for signed and unsigned types separately.
template<typename Type, bool is_signed>
struct WithOverflowCheck {
};


// Specialization for signed types.
template<typename Type>
struct WithOverflowCheck<Type, true> {
  static inline Type Add(Type a, Type b, bool *overflowed) {
    // Implementation from the CERT article referenced in the file header.
    *overflowed = (((a > 0) && (b > 0) && (a > (MathLimits<Type>::kMax - b))) ||
                   ((a < 0) && (b < 0) && (a < (MathLimits<Type>::kMin - b))));
    return a + b;
  }
};

// Specialization for unsigned types.
template<typename Type>
struct WithOverflowCheck<Type, false> {
  static inline Type Add(Type a, Type b, bool *overflowed) {
    Type ret = a + b;
    *overflowed = ret < a;
    return a + b;
  }
};

} // namespace safe_math_internal

// Add 'a' and 'b', and set *overflowed to true if overflow occurred.
template<typename Type>
inline Type AddWithOverflowCheck(Type a, Type b, bool *overflowed) {
  // Pick the right specialization based on whether Type is signed.
  typedef safe_math_internal::WithOverflowCheck<Type, MathLimits<Type>::kIsSigned> my_struct;
  return my_struct::Add(a, b, overflowed);
}

} // namespace yb
