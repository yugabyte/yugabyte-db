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
#ifndef YB_CLIENT_VALUE_H
#define YB_CLIENT_VALUE_H

#ifdef YB_HEADERS_NO_STUBS
#include "yb/gutil/macros.h"
#include "yb/gutil/port.h"
#else
#include "yb/client/stubs.h"
#endif
#include "yb/util/slice.h"
#include "yb/util/yb_export.h"

namespace yb {
namespace client {

// A constant cell value with a specific type.
class YB_EXPORT YBValue {
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
  friend class KuduColumnSpec;

  class YB_NO_EXPORT Data;
  explicit YBValue(Data* d);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(YBValue);
};

} // namespace client
} // namespace yb
#endif /* YB_CLIENT_VALUE_H */
