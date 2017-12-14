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

#include "yb/common/hybrid_time.h"

#include "yb/util/memcmpable_varint.h"

using std::string;
using strings::Substitute;
using strings::SubstituteAndAppend;

namespace yb {

namespace {

MicrosTime base_time_for_to_string_in_tests_ = 0;

}

const HybridTime HybridTime::kMin(kMinHybridTimeValue);
const HybridTime HybridTime::kMax(kMaxHybridTimeValue);
const HybridTime HybridTime::kInitialHybridTime(kInitialHybridTimeValue);
const HybridTime HybridTime::kInvalidHybridTime(kInvalidHybridTimeValue);

bool HybridTime::DecodeFrom(Slice *input) {
  return GetMemcmpableVarint64(input, &v).ok();
}

void HybridTime::AppendAsUint64To(faststring *dst) const {
  PutMemcmpableVarint64(dst, v);
}

void HybridTime::AppendAsUint64To(std::string* dst) const {
  PutMemcmpableVarint64(dst, v);
}

string HybridTime::ToString() const {
  switch (v) {
    case kInvalidHybridTimeValue:
      return "<invalid>";
    case kMaxHybridTimeValue:
      return "<max>";
    case kMinHybridTimeValue:
      return "<min>";
    case kInitialHybridTimeValue:
      return "<initial>";
    default:
      return Format("{ physical: $0 logical: $1 }",
                    GetPhysicalValueMicros() - base_time_for_to_string_in_tests_,
                    GetLogicalValue());
  }
}

void HybridTime::TEST_SetBaseTimeForToString(MicrosTime micros) {
  base_time_for_to_string_in_tests_ = micros;
}

string HybridTime::ToDebugString() const {
  string s(kHybridTimeDebugStrPrefix);
  s += "(";
  switch (v) {
    case kMinHybridTimeValue: s += "Min"; break;
    case kInitialHybridTimeValue: s += "Initial"; break;

    // For Max and Invalid, don't put the value in the string because it is a large constant.
    case kMaxHybridTimeValue: s += "Max"; break;
    case kInvalidHybridTimeValue: s += "Invalid"; break;
    default:
      SubstituteAndAppend(&s, "p=$0", GetPhysicalValueMicros());
      const LogicalTimeComponent logical = GetLogicalValue();
      if (logical != 0) {
        SubstituteAndAppend(&s, ", l=$0", logical);
      }
  }
  s += ")";
  return s;
}

uint64_t HybridTime::ToUint64() const {
  return v;
}

Status HybridTime::FromUint64(uint64_t value) {
  v = value;
  return Status::OK();
}

const char* const HybridTime::kHybridTimeDebugStrPrefix = "HT";

}  // namespace yb
