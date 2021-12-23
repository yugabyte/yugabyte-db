//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
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
// This module defines standard C++ functions that are used to support QL builtin functions.
// Each of these functions have one or more entries in builtin library directory. Note that C++
// functions don't have to be defined here as long as they are linked to this lib.
//
// Once written, this function should not be changed to avoid compatibility issues. That is,
// server might runs one version while client use a different version of this function.
//
// See the header of file "/util/bfql/bfql.h" for more general info.
//--------------------------------------------------------------------------------------------------

#ifndef YB_BFQL_BFUNC_STANDARD_H_
#define YB_BFQL_BFUNC_STANDARD_H_

#include <fcntl.h>
#include <stdint.h>
#include <uuid/uuid.h>

#include <cstring>
#include <iostream>
#include <locale>
#include <string>
#include <unordered_set>

#include "yb/common/jsonb.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/common/ql_type.h"

#include "yb/util/status_fwd.h"
#include "yb/util/date_time.h"
#include "yb/util/fault_injection.h"
#include "yb/util/logging.h"
#include "yb/util/status_log.h"
#include "yb/util/string_util.h"
#include "yb/util/uuid.h"
#include "yb/util/yb_partition.h"

namespace yb {

bool operator ==(const QLValuePB& lhs, const QLValuePB& rhs);

namespace bfql {

//--------------------------------------------------------------------------------------------------
// Dummy function for minimum opcode.
inline CHECKED_STATUS NoOp() {
  return Status::OK();
}

// ServerOperator that takes no argument and has no return value.
inline CHECKED_STATUS ServerOperator() {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// ServerOperator that takes 1 argument and has a return value.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ServerOperator(PTypePtr arg1, RTypePtr result) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// This is not used but implemented as an example for future coding.
// ServerOperator that takes 2 arguments and has a return value.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ServerOperator(PTypePtr arg1, PTypePtr arg2, RTypePtr result) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

//--------------------------------------------------------------------------------------------------

template<typename PTypePtr, typename RTypePtr>
uint16_t YBHash(const vector<PTypePtr>& params, RTypePtr result) {
  string encoded_key = "";
  for (int i = 0; i < params.size(); i++) {
    const PTypePtr& param = params[i];
    param->AppendToKeyBytes(&encoded_key);
  }

  return YBPartition::HashColumnCompoundValue(encoded_key);
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS Token(const vector<PTypePtr>& params, RTypePtr result) {
  uint16_t hash = YBHash(params, result);
  // Convert to CQL hash since this may be used in expressions above.
  result->set_int64_value(YBPartition::YBToCqlHashCode(hash));
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS PartitionHash(const vector<PTypePtr>& params, RTypePtr result) {
  result->set_int32_value(YBHash(params, result));
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ToJson(PTypePtr col, RTypePtr result) {
  common::Jsonb jsonb;
  Status s = jsonb.FromQLValuePB(col->value());

  if (!s.ok()) {
    return s.CloneAndPrepend(strings::Substitute(
        "Cannot convert $0 value $1 to $2",
        QLType::ToCQLString(InternalToDataType(col->type())),
        col->ToString(),
        QLType::ToCQLString(DataType::JSONB)));
  }

  result->set_jsonb_value(jsonb.MoveSerializedJsonb());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ttl(PTypePtr col, RTypePtr result) {
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS writetime(PTypePtr col, RTypePtr result) {
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Special ops for counter: "+counter" and "-counter".

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS IncCounter(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull()) {
    result->set_int64_value(y->int64_value());
  } else {
    result->set_int64_value(x->int64_value() + y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS DecCounter(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull()) {
    result->set_int64_value(-y->int64_value());
  } else {
    result->set_int64_value(x->int64_value() - y->int64_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// "+" and "-".

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS AddI64I64(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_int64_value(x->int64_value() + y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS AddDoubleDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_double_value(x->double_value() + y->double_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS AddStringString(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_string_value(x->string_value() + y->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS AddStringDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_string_value(x->string_value() + std::to_string(y->double_value()));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS AddDoubleString(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_string_value(std::to_string(x->double_value()) + y->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS AddMapMap(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS AddSetSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS AddListList(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS SubI64I64(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_int64_value(x->int64_value() - y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS SubDoubleDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (x->IsNull() || y->IsNull()) {
    result->SetNull();
  } else {
    result->set_double_value(x->double_value() - y->double_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS SubMapSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS SubSetSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS SubListList(PTypePtr x, PTypePtr y, RTypePtr result) {
  // TODO All calls allowed for this builtin should be optimized away to avoid evaluating here.
  // But this is not yet implemented in DocDB so evaluating inefficiently and in-memory for now.
  // For clarity, this implementation should be removed (see e.g. SubSetSet above) as soon as
  // RemoveFromList is implemented in DocDB.
  result->set_list_value();
  if (x->IsNull() || y->IsNull()) {
    return Status::OK();
  }

  const QLSeqValuePB& xl = x->list_value();
  const QLSeqValuePB& yl = y->list_value();
  for (const QLValuePB& x_elem : xl.elems()) {
    bool should_remove = false;
    for (const QLValuePB& y_elem : yl.elems()) {
      if (x_elem == y_elem) {
        should_remove = true;
        break;
      }
    }
    if (!should_remove) {
      result->add_list_elem()->CopyFrom(x_elem);
    }
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Now().
template<typename RTypePtr>
CHECKED_STATUS NowDate(RTypePtr result) {
  result->set_date_value(DateTime::DateNow());
  return Status::OK();
}

template<typename RTypePtr>
CHECKED_STATUS NowTime(RTypePtr result) {
  result->set_time_value(DateTime::TimeNow());
  return Status::OK();
}

template<typename RTypePtr>
CHECKED_STATUS NowTimestamp(RTypePtr result) {
  result->set_timestamp_value(DateTime::TimestampNow());
  return Status::OK();
}

template<typename RTypePtr>
CHECKED_STATUS NowTimeUuid(RTypePtr result) {
  uuid_t linux_time_uuid;
  uuid_generate_time(linux_time_uuid);
  Uuid time_uuid(linux_time_uuid);
  CHECK_OK(time_uuid.IsTimeUuid());
  CHECK_OK(time_uuid.HashMACAddress());
  result->set_timeuuid_value(time_uuid);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// uuid().
static const uint16_t kUUIDType = 4;
template<typename RTypePtr>
CHECKED_STATUS GetUuid(RTypePtr result) {
  Uuid uuid = Uuid::Generate();
  DCHECK_EQ(uuid.version(), kUUIDType);
  if (uuid.version() != kUUIDType) {
    return STATUS_FORMAT(IllegalState, "Unexpected UUID type $0, expected $1.",
                         uuid.version(), kUUIDType);
  }
  result->set_uuid_value(uuid);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Map::Map
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS MapConstructor(const vector<PTypePtr>& params, RTypePtr result) {
  auto *qlmap = result->mutable_map_value();
  RSTATUS_DCHECK(params.size()%2 == 0, RuntimeError, "Unexpected argument count for map::map");
  for (int i = 0; i < params.size(); i++) {
    *qlmap->add_keys() = params[i]->value();
    *qlmap->add_values() = params[++i]->value();
  }
  return Status::OK();
}

// Set::Set
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS SetConstructor(const vector<PTypePtr>& params, RTypePtr result) {
  auto *qlset = result->mutable_set_value();
  for (int i = 0; i < params.size(); i++) {
    *qlset->add_elems() = params[i]->value();
  }
  return Status::OK();
}

// List::List
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ListConstructor(const vector<PTypePtr>& params, RTypePtr result) {
  auto *qllist = result->mutable_list_value();
  for (int i = 0; i < params.size(); i++) {
    *qllist->add_elems() = params[i]->value();
  }
  return Status::OK();
}

// Map::Frozen
template<typename PType>
std::map<PType, PType> MapFromVector(const std::vector<PType*>& params) {
  std::map<PType, PType> ordered_values;
  for (int vidx = 0; vidx < params.size(); vidx++) {
    int kidx = vidx++;
    ordered_values[*params[kidx]] = *params[vidx];
  }
  return ordered_values;
}

template<typename PType>
std::map<PType, PType> MapFromVector(const std::vector<std::shared_ptr<PType>>& params) {
  std::map<PType, PType> ordered_values;
  for (int vidx = 0; vidx < params.size(); vidx++) {
    int kidx = vidx++;
    ordered_values[*params[kidx]] = *params[vidx];
  }
  return ordered_values;
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS MapFrozen(const vector<PTypePtr>& params, RTypePtr result) {
  auto map_elems = MapFromVector(params);

  auto *frozen_value = result->mutable_frozen_value();
  for (auto &elem : map_elems) {
    *frozen_value->add_elems() = elem.first.value();
    *frozen_value->add_elems() = elem.second.value();
  }
  return Status::OK();
}

// Set::Frozen.
template<typename PType>
std::set<PType> SetFromVector(const std::vector<PType*>& params) {
  std::set<PType> ordered_values;
  for (int i = 0; i < params.size(); i++) {
    ordered_values.insert(*params[i]);
  }
  return ordered_values;
}

template<typename PType>
std::set<PType> SetFromVector(const std::vector<std::shared_ptr<PType>>& params) {
  std::set<PType> ordered_values;
  for (int i = 0; i < params.size(); i++) {
    ordered_values.insert(*params[i]);
  }
  return ordered_values;
}

template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS SetFrozen(const vector<PTypePtr>& params, RTypePtr result) {
  auto set_elems = SetFromVector(params);

  auto *frozen_value = result->mutable_frozen_value();
  for (auto &elem : set_elems) {
    *frozen_value->add_elems() = elem.value();
  }
  return Status::OK();
}

// List::Frozen.
template<typename PTypePtr, typename RTypePtr>
CHECKED_STATUS ListFrozen(const vector<PTypePtr>& params, RTypePtr result) {
  auto *frozen_value = result->mutable_frozen_value();
  for (int i = 0; i < params.size(); i++) {
    *frozen_value->add_elems() = params[i]->value();
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

} // namespace bfql
} // namespace yb

#endif  // YB_BFQL_BFUNC_STANDARD_H_
