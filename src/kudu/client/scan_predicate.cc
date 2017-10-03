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

#include "kudu/client/scan_predicate.h"
#include "kudu/client/scan_predicate-internal.h"
#include "kudu/client/value.h"
#include "kudu/client/value-internal.h"

#include "kudu/common/scan_spec.h"
#include "kudu/common/scan_predicate.h"

#include "kudu/gutil/strings/substitute.h"

using strings::Substitute;

namespace kudu {
namespace client {

KuduPredicate::KuduPredicate(Data* d)
  : data_(d) {
}

KuduPredicate::~KuduPredicate() {
  delete data_;
}

KuduPredicate::Data::Data() {
}

KuduPredicate::Data::~Data() {
}

KuduPredicate* KuduPredicate::Clone() const {
  return new KuduPredicate(data_->Clone());
}

ComparisonPredicateData::ComparisonPredicateData(ColumnSchema col,
                                                 KuduPredicate::ComparisonOp op,
                                                 KuduValue* val)
    : col_(std::move(col)),
      op_(op),
      val_(val) {
}
ComparisonPredicateData::~ComparisonPredicateData() {
}


Status ComparisonPredicateData::AddToScanSpec(ScanSpec* spec) {
  void* val_void;
  RETURN_NOT_OK(val_->data_->CheckTypeAndGetPointer(col_.name(),
                                                    col_.type_info()->physical_type(),
                                                    &val_void));

  void* lower_bound = nullptr;
  void* upper_bound = nullptr;
  switch (op_) {
    case KuduPredicate::LESS_EQUAL:
      upper_bound = val_void;
      break;
    case KuduPredicate::GREATER_EQUAL:
      lower_bound = val_void;
      break;
    case KuduPredicate::EQUAL:
      lower_bound = upper_bound = val_void;
      break;
    default:
      return Status::InvalidArgument(Substitute("invalid comparison op: $0", op_));
  }

  ColumnRangePredicate p(col_, lower_bound, upper_bound);
  spec->AddPredicate(p);

  return Status::OK();
}

} // namespace client
} // namespace kudu
