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
#ifndef KUDU_CLIENT_VALUE_INTERNAL_H
#define KUDU_CLIENT_VALUE_INTERNAL_H

#include <string>

#include "kudu/common/types.h"
#include "kudu/gutil/macros.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

namespace kudu {
namespace client {

class KuduValue::Data {
 public:
  enum Type {
    INT,
    FLOAT,
    DOUBLE,
    SLICE
  };
  Type type_;
  union {
    int64_t int_val_;
    float float_val_;
    double double_val_;
  };
  Slice slice_val_;

  // Check that this value can be converted to the given datatype 't',
  // and return a pointer to the underlying value in '*val_void'.
  //
  // 'col_name' is used to generate reasonable error messages in the case
  // that the type cannot be coerced.
  //
  // The returned pointer in *val_void is only guaranteed to live as long
  // as this KuduValue object.
  Status CheckTypeAndGetPointer(const std::string& col_name,
                                DataType t,
                                void** val_void);

 private:
  // Check that this value has the expected type 'type', returning
  // a nice error Status if not.
  Status CheckValType(const std::string& col_name,
                      KuduValue::Data::Type type,
                      const char* type_str) const;

  // Check that this value is a boolean constant, and set *val_void to
  // point to it if so.
  Status CheckAndPointToBool(const std::string& col_name, void** val_void);

  // Check that this value is an integer constant within the valid range,
  // and set *val_void to point to it if so.
  Status CheckAndPointToInt(const std::string& col_name,
                            size_t int_size, void** val_void);

  // Check that this value is a string constant, and set *val_void to
  // point to it if so.
  Status CheckAndPointToString(const std::string& col_name,
                               void** val_void);
};

} // namespace client
} // namespace kudu
#endif /* KUDU_CLIENT_VALUE_INTERNAL_H */
