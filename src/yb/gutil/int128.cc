// Copyright 2004 Google Inc.
// All Rights Reserved.
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
//

#include <iostream>
#include "yb/gutil/int128.h"
#include "yb/gutil/integral_types.h"

const uint128_pod kuint128max = {
    static_cast<uint64>(GG_LONGLONG(0xFFFFFFFFFFFFFFFF)),
    static_cast<uint64>(GG_LONGLONG(0xFFFFFFFFFFFFFFFF))
};

std::ostream& operator<<(std::ostream& o, const uint128& b) {
  return (o << b.hi_ << "::" << b.lo_);
}
