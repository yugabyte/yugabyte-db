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

#pragma once

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
inline Status NoOp() {
  return Status::OK();
}

// ServerOperator that takes no argument and has no return value.
inline Status ServerOperator() {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// ServerOperator that takes 1 argument and has a return value.
template<typename PTypePtr, typename RTypePtr>
Status ServerOperator(PTypePtr arg1, RTypePtr result) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

// This is not used but implemented as an example for future coding.
// ServerOperator that takes 2 arguments and has a return value.
template<typename PTypePtr, typename RTypePtr>
Status ServerOperator(PTypePtr arg1, PTypePtr arg2, RTypePtr result) {
  LOG(ERROR) << "Only tablet servers can execute this builtin call";
  return STATUS(RuntimeError, "Only tablet servers can execute this builtin call");
}

//--------------------------------------------------------------------------------------------------

template<typename PTypePtr, typename RTypePtr>
uint16_t YBHash(const std::vector<PTypePtr>& params, RTypePtr result) {
  std::string encoded_key = "";
  for (const PTypePtr& param : params) {
    AppendToKey(*param, &encoded_key);
  }

  return YBPartition::HashColumnCompoundValue(encoded_key);
}

template<typename PTypePtr, typename RTypePtr>
Status Token(const std::vector<PTypePtr>& params, RTypePtr result) {
  uint16_t hash = YBHash(params, result);
  // Convert to CQL hash since this may be used in expressions above.
  result->set_int64_value(YBPartition::YBToCqlHashCode(hash));
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status PartitionHash(const std::vector<PTypePtr>& params, RTypePtr result) {
  result->set_int32_value(YBHash(params, result));
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ToJson(PTypePtr col, RTypePtr result) {
  common::Jsonb jsonb;
  Status s = jsonb.FromQLValue(*col);

  if (!s.ok()) {
    return s.CloneAndPrepend(Format(
        "Cannot convert $0 value $1 to $2",
        QLType::ToCQLString(InternalToDataType(col->value_case())),
        *col,
        QLType::ToCQLString(DataType::JSONB)));
  }

  result->set_jsonb_value(jsonb.MoveSerializedJsonb());
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status ttl(PTypePtr col, RTypePtr result) {
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status writetime(PTypePtr col, RTypePtr result) {
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Special ops for counter: "+counter" and "-counter".

template<typename PTypePtr, typename RTypePtr>
Status IncCounter(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (IsNull(*x)) {
    result->set_int64_value(y->int64_value());
  } else {
    result->set_int64_value(x->int64_value() + y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status DecCounter(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (IsNull(*x)) {
    result->set_int64_value(-y->int64_value());
  } else {
    result->set_int64_value(x->int64_value() - y->int64_value());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// "+" and "-".

template<typename PTypePtr, typename RTypePtr>
Status AddI64I64(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (IsNull(*x) || IsNull(*y)) {
    SetNull(&*result);
  } else {
    result->set_int64_value(x->int64_value() + y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddDoubleDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (IsNull(*x) || IsNull(*y)) {
    SetNull(&*result);
  } else {
    result->set_double_value(x->double_value() + y->double_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddStringString(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (IsNull(*x) || IsNull(*y)) {
    SetNull(&*result);
  } else {
    result->set_string_value(x->string_value() + y->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddStringDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (IsNull(*x) || IsNull(*y)) {
    SetNull(&*result);
  } else {
    result->set_string_value(x->string_value() + std::to_string(y->double_value()));
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddDoubleString(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (IsNull(*x) || IsNull(*y)) {
    SetNull(&*result);
  } else {
    result->set_string_value(std::to_string(x->double_value()) + y->string_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status AddMapMap(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status AddSetSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status AddListList(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status SubI64I64(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (IsNull(*x) || IsNull(*y)) {
    SetNull(&*result);
  } else {
    result->set_int64_value(x->int64_value() - y->int64_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status SubDoubleDouble(PTypePtr x, PTypePtr y, RTypePtr result) {
  if (IsNull(*x) || IsNull(*y)) {
    SetNull(&*result);
  } else {
    result->set_double_value(x->double_value() - y->double_value());
  }
  return Status::OK();
}

template<typename PTypePtr, typename RTypePtr>
Status SubMapSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status SubSetSet(PTypePtr x, PTypePtr y, RTypePtr result) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

template<typename PTypePtr, typename RTypePtr>
Status SubListList(PTypePtr x, PTypePtr y, RTypePtr result) {
  // TODO All calls allowed for this builtin should be optimized away to avoid evaluating here.
  // But this is not yet implemented in DocDB so evaluating inefficiently and in-memory for now.
  // For clarity, this implementation should be removed (see e.g. SubSetSet above) as soon as
  // RemoveFromList is implemented in DocDB.
  result->set_list_value();
  if (IsNull(*x) || IsNull(*y)) {
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
Status NowDate(RTypePtr result) {
  result->set_date_value(DateTime::DateNow());
  return Status::OK();
}

template<typename RTypePtr>
Status NowTime(RTypePtr result) {
  result->set_time_value(DateTime::TimeNow());
  return Status::OK();
}

template<typename RTypePtr>
Status NowTimestamp(RTypePtr result) {
  result->set_timestamp_value(DateTime::TimestampNow().ToInt64());
  return Status::OK();
}

template<typename RTypePtr>
Status NowTimeUuid(RTypePtr result) {
  uuid_t linux_time_uuid;
  uuid_generate_time(linux_time_uuid);
  Uuid time_uuid(linux_time_uuid);
  CHECK_OK(time_uuid.IsTimeUuid());
  CHECK_OK(time_uuid.HashMACAddress());
  QLValue::set_timeuuid_value(time_uuid, &*result);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// uuid().
static const uint16_t kUUIDType = 4;
template<typename RTypePtr>
Status GetUuid(RTypePtr result) {
  Uuid uuid = Uuid::Generate();
  DCHECK_EQ(uuid.version(), kUUIDType);
  if (uuid.version() != kUUIDType) {
    return STATUS_FORMAT(IllegalState, "Unexpected UUID type $0, expected $1.",
                         uuid.version(), kUUIDType);
  }
  QLValue::set_uuid_value(uuid, &*result);
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------
// Map::Map
template<typename PTypePtr, typename RTypePtr>
Status MapConstructor(const std::vector<PTypePtr>& params, RTypePtr result) {
  auto *qlmap = result->mutable_map_value();
  RSTATUS_DCHECK(params.size()%2 == 0, RuntimeError, "Unexpected argument count for map::map");
  for (size_t i = 0; i < params.size(); i++) {
    QLValue::set_value(*params[i], qlmap->add_keys());
    QLValue::set_value(*params[++i], qlmap->add_values());
  }
  return Status::OK();
}

// Set::Set
template<typename PTypePtr, typename RTypePtr>
Status SetConstructor(const std::vector<PTypePtr>& params, RTypePtr result) {
  auto *qlset = result->mutable_set_value();
  for (const auto& param : params) {
    QLValue::set_value(*param, qlset->add_elems());
  }
  return Status::OK();
}

// List::List
template<typename PTypePtr, typename RTypePtr>
Status ListConstructor(const std::vector<PTypePtr>& params, RTypePtr result) {
  auto *qllist = result->mutable_list_value();
  for (const auto& param : params) {
    QLValue::set_value(*param, qllist->add_elems());
  }
  return Status::OK();
}

// Map::Frozen
template<typename PType>
std::map<PType, PType> MapFromVector(const std::vector<PType*>& params) {
  std::map<PType, PType> ordered_values;
  for (size_t vidx = 0; vidx < params.size(); vidx++) {
    auto kidx = vidx++;
    ordered_values[*params[kidx]] = *params[vidx];
  }
  return ordered_values;
}

template<typename PType>
std::map<PType, PType> MapFromVector(const std::vector<std::shared_ptr<PType>>& params) {
  std::map<PType, PType> ordered_values;
  for (size_t vidx = 0; vidx < params.size(); vidx++) {
    auto kidx = vidx++;
    ordered_values[*params[kidx]] = *params[vidx];
  }
  return ordered_values;
}

template<typename PTypePtr, typename RTypePtr>
Status MapFrozen(const std::vector<PTypePtr>& params, RTypePtr result) {
  auto map_elems = MapFromVector(params);

  auto *frozen_value = result->mutable_frozen_value();
  for (auto &elem : map_elems) {
    QLValue::set_value(elem.first, frozen_value->add_elems());
    QLValue::set_value(elem.second, frozen_value->add_elems());
  }
  return Status::OK();
}

// Set::Frozen.
template<typename PType>
std::set<PType> SetFromVector(const std::vector<PType*>& params) {
  std::set<PType> ordered_values;
  for (size_t i = 0; i < params.size(); i++) {
    ordered_values.insert(*params[i]);
  }
  return ordered_values;
}

template<typename PType>
std::set<PType> SetFromVector(const std::vector<std::shared_ptr<PType>>& params) {
  std::set<PType> ordered_values;
  for (const auto& param : params) {
    ordered_values.insert(*param);
  }
  return ordered_values;
}

template<typename PTypePtr, typename RTypePtr>
Status SetFrozen(const std::vector<PTypePtr>& params, RTypePtr result) {
  auto set_elems = SetFromVector(params);

  auto *frozen_value = result->mutable_frozen_value();
  for (auto &elem : set_elems) {
    QLValue::set_value(elem, frozen_value->add_elems());
  }
  return Status::OK();
}

// List::Frozen.
template<typename PTypePtr, typename RTypePtr>
Status ListFrozen(const std::vector<PTypePtr>& params, RTypePtr result) {
  auto *frozen_value = result->mutable_frozen_value();
  for (const auto& param : params) {
    QLValue::set_value(*param, frozen_value->add_elems());
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

} // namespace bfql
} // namespace yb
