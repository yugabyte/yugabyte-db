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
#ifndef KUDU_CLIENT_VALUE_H
#define KUDU_CLIENT_VALUE_H

#ifdef KUDU_HEADERS_NO_STUBS
#include "kudu/gutil/macros.h"
#include "kudu/gutil/port.h"
#else
#include "kudu/client/stubs.h"
#endif
#include "kudu/util/slice.h"
#include "kudu/util/kudu_export.h"

namespace kudu {
namespace client {

// A constant cell value with a specific type.
class KUDU_EXPORT KuduValue {
 public:
  // Return a new identical KuduValue object.
  KuduValue* Clone() const;

  // Construct a KuduValue from the given integer.
  static KuduValue* FromInt(int64_t v);

  // Construct a KuduValue from the given float.
  static KuduValue* FromFloat(float f);

  // Construct a KuduValue from the given double.
  static KuduValue* FromDouble(double d);

  // Construct a KuduValue from the given bool.
  static KuduValue* FromBool(bool b);

  // Construct a KuduValue by copying the value of the given Slice.
  static KuduValue* CopyString(Slice s);

  ~KuduValue();
 private:
  friend class ComparisonPredicateData;
  friend class KuduColumnSpec;

  class KUDU_NO_EXPORT Data;
  explicit KuduValue(Data* d);

  // Owned.
  Data* data_;

  DISALLOW_COPY_AND_ASSIGN(KuduValue);
};

} // namespace client
} // namespace kudu
#endif /* KUDU_CLIENT_VALUE_H */
