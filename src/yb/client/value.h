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
#pragma once

#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#include "yb/util/slice.h"

namespace yb {
namespace client {

// A constant cell value with a specific type.
class YBValue {
 public:
  // Return a new identical YBValue object.
  YBValue* Clone() const;

  // Construct a YBValue from the given integer.
  static YBValue* FromInt(int64_t v);

  // Construct a YBValue from the given float.
  static YBValue* FromFloat(float f);

  // Construct a YBValue from the given double.
  static YBValue* FromDouble(double d);

  // Construct a YBValue from the given bool.
  static YBValue* FromBool(bool b);

  // Construct a YBValue by copying the value of the given Slice.
  static YBValue* CopyString(Slice s);

  ~YBValue();
 private:
  friend class ComparisonPredicateData;
  friend class YBColumnSpec;

  class Data;
  explicit YBValue(Data* d);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(YBValue);
};

} // namespace client
} // namespace yb
