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

#include "yb/util/atomic.h"

#include <stdint.h>

#include "yb/util/logging.h"

namespace yb {

template<typename T>
AtomicInt<T>::AtomicInt(T initial_value) {
  Store(initial_value, kMemOrderNoBarrier);
}

template<typename T>
void AtomicInt<T>::FatalMemOrderNotSupported(const char* caller,
                                             const char* requested,
                                             const char* supported) {
  LOG(FATAL) << caller << " does not support " << requested << ": only "
             << supported << " are supported.";
}

template
class AtomicInt<int32_t>;

template
class AtomicInt<int64_t>;

template
class AtomicInt<uint32_t>;

template
class AtomicInt<uint64_t>;

AtomicBool::AtomicBool(bool value)
    : underlying_(value) {
}

} // namespace yb
