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

#include "yb/client/value.h"

#include "yb/util/logging.h"

#include "yb/client/value-internal.h"
#include "yb/common/ql_type.h"
#include "yb/common/types.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/mathlimits.h"
#include "yb/util/status.h"

using std::shared_ptr;
using std::string;
using strings::Substitute;

namespace yb {
namespace client {

YBValue::YBValue(Data* d)
: data_(d) {
}

YBValue::~YBValue() {
  if (data_->type_ == Data::SLICE) {
    delete[] data_->slice_val_.data();
  }
  delete data_;
}

YBValue* YBValue::Clone() const {
  switch (data_->type_) {
    case Data::INT:
      return YBValue::FromInt(data_->int_val_);
    case Data::DOUBLE:
      return YBValue::FromDouble(data_->double_val_);
    case Data::FLOAT:
      return YBValue::FromFloat(data_->float_val_);
    case Data::SLICE:
      return YBValue::CopyString(data_->slice_val_);
  }
  LOG(FATAL);
}

YBValue* YBValue::FromInt(int64_t v) {
  auto d = new Data;
  d->type_ = Data::INT;
  d->int_val_ = v;

  return new YBValue(d);
}

YBValue* YBValue::FromDouble(double v) {
  auto d = new Data;
  d->type_ = Data::DOUBLE;
  d->double_val_ = v;

  return new YBValue(d);
}


YBValue* YBValue::FromFloat(float v) {
  auto d = new Data;
  d->type_ = Data::FLOAT;
  d->float_val_ = v;

  return new YBValue(d);
}

YBValue* YBValue::FromBool(bool v) {
  auto d = new Data;
  d->type_ = Data::INT;
  d->int_val_ = v ? 1 : 0;

  return new YBValue(d);
}

YBValue* YBValue::CopyString(Slice s) {
  auto copy = new uint8_t[s.size()];
  memcpy(copy, s.data(), s.size());

  auto d = new Data;
  d->type_ = Data::SLICE;
  d->slice_val_ = Slice(copy, s.size());

  return new YBValue(d);
}

Status YBValue::Data::CheckTypeAndGetPointer(const string& col_name,
                                             const shared_ptr<QLType>& tp,
                                             void** val_void) {
  const TypeInfo* ti = tp->type_info();
  switch (ti->physical_type) {
    case yb::INT8:
    case yb::INT16:
    case yb::INT32:
    case yb::INT64:
      RETURN_NOT_OK(CheckAndPointToInt(col_name, ti->size, val_void));
      break;

    case yb::BOOL:
      RETURN_NOT_OK(CheckAndPointToBool(col_name, val_void));
      break;

    case yb::FLOAT:
      RETURN_NOT_OK(CheckValType(col_name, YBValue::Data::FLOAT, "float"));
      *val_void = &float_val_;
      break;

    case yb::DOUBLE:
      RETURN_NOT_OK(CheckValType(col_name, YBValue::Data::DOUBLE, "double"));
      *val_void = &double_val_;
      break;

    case yb::BINARY:
      RETURN_NOT_OK(CheckAndPointToString(col_name, val_void));
      break;

    default:
      return STATUS(InvalidArgument, Substitute("cannot determine value for column $0 (type $1)",
                                                col_name, ti->name));
  }
  return Status::OK();
}

Status YBValue::Data::CheckValType(const string& col_name,
                                     YBValue::Data::Type type,
                                     const char* type_str) const {
  if (type_ != type) {
    return STATUS(InvalidArgument,
        Substitute("non-$0 value for $0 column $1", type_str, col_name));
  }
  return Status::OK();
}

Status YBValue::Data::CheckAndPointToBool(const string& col_name,
                                            void** val_void) {
  RETURN_NOT_OK(CheckValType(col_name, YBValue::Data::INT, "bool"));
  int64_t int_val = int_val_;
  if (int_val != 0 && int_val != 1) {
    return STATUS(InvalidArgument,
        Substitute("value $0 out of range for boolean column '$1'",
                   int_val, col_name));
  }
  *val_void = &int_val_;
  return Status::OK();
}

Status YBValue::Data::CheckAndPointToInt(const string& col_name,
                                           size_t int_size,
                                           void** val_void) {
  RETURN_NOT_OK(CheckValType(col_name, YBValue::Data::INT, "int"));

  int64_t int_min, int_max;
  if (int_size == 8) {
    int_min = MathLimits<int64_t>::kMin;
    int_max = MathLimits<int64_t>::kMax;
  } else {
    size_t int_bits = int_size * 8 - 1;
    int_max = (1LL << int_bits) - 1;
    int_min = -int_max - 1;
  }

  int64_t int_val = int_val_;
  if (int_val < int_min || int_val > int_max) {
    return STATUS(InvalidArgument,
        Substitute("value $0 out of range for $1-bit signed integer column '$2'",
                   int_val, int_size * 8, col_name));
  }

  *val_void = &int_val_;
  return Status::OK();
}

Status YBValue::Data::CheckAndPointToString(const string& col_name,
                                              void** val_void) {
  RETURN_NOT_OK(CheckValType(col_name, YBValue::Data::SLICE, "string"));
  *val_void = &slice_val_;
  return Status::OK();
}

} // namespace client
} // namespace yb
