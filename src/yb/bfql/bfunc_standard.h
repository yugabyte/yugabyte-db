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

#include "yb/bfcommon/bfunc_standard.h"

#include "yb/bfql/bfdecl.h"

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

using bfcommon::NoOp;
using bfcommon::ServerOperator;
using bfcommon::AddI64I64;
using bfcommon::AddDoubleDouble;
using bfcommon::AddStringString;
using bfcommon::AddStringDouble;
using bfcommon::AddDoubleString;
using bfcommon::SubI64I64;
using bfcommon::SubDoubleDouble;
using bfcommon::NowTimeUuid;

//--------------------------------------------------------------------------------------------------

inline uint16_t YBHash(const BFParams& params) {
  std::string encoded_key = "";
  for (const auto& param : params) {
    AppendToKey(param, &encoded_key);
  }

  return YBPartition::HashColumnCompoundValue(encoded_key);
}

inline Result<BFRetValue> Token(const BFParams& params, BFFactory factory) {
  BFRetValue result = factory();
  // Convert to CQL hash since this may be used in expressions above.
  result.set_int64_value(YBPartition::YBToCqlHashCode(YBHash(params)));
  return result;
}

inline Result<BFRetValue> PartitionHash(const BFParams& params, BFFactory factory) {
  BFRetValue result = factory();
  result.set_int32_value(YBHash(params));
  return result;
}

inline Result<BFRetValue> ToJson(const BFValue& col, BFFactory factory) {
  common::Jsonb jsonb;
  Status s = jsonb.FromQLValue(col);

  RETURN_NOT_OK_PREPEND(
      jsonb.FromQLValue(col),
      Format("Cannot convert $0 value $1 to $2",
             QLType::ToCQLString(InternalToDataType(col.value_case())), col,
             QLType::ToCQLString(DataType::JSONB)));
  BFRetValue result = factory();
  result.set_jsonb_value(jsonb.MoveSerializedJsonb());
  return result;
}

inline Result<BFRetValue> ttl(const BFValue& col, BFFactory factory) {
  return factory();
}

inline Result<BFRetValue> writetime(const BFValue& col, BFFactory factory) {
  return factory();
}

//--------------------------------------------------------------------------------------------------
// Special ops for counter: "+counter" and "-counter".

inline Result<BFRetValue> IncCounter(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  if (IsNull(x)) {
    result.set_int64_value(y.int64_value());
  } else {
    result.set_int64_value(x.int64_value() + y.int64_value());
  }
  return result;
}

inline Result<BFRetValue> DecCounter(const BFValue& x, const BFValue& y, BFFactory factory) {
  BFRetValue result = factory();
  if (IsNull(x)) {
    result.set_int64_value(-y.int64_value());
  } else {
    result.set_int64_value(x.int64_value() - y.int64_value());
  }
  return result;
}

inline Result<BFRetValue> AddMapMap(const BFValue& x, const BFValue& y, BFFactory factory) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

inline Result<BFRetValue> AddSetSet(const BFValue& x, const BFValue& y, BFFactory factory) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

inline Result<BFRetValue> AddListList(const BFValue& x, const BFValue& y, BFFactory factory) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

inline Result<BFRetValue> SubMapSet(const BFValue& x, const BFValue& y, BFFactory factory) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

inline Result<BFRetValue> SubSetSet(const BFValue& x, const BFValue& y, BFFactory factory) {
  // All calls allowed for this builtin are optimized away to avoid evaluating such expressions
  return STATUS(RuntimeError, "Arbitrary collection expressions are not supported");
}

inline Result<BFRetValue> SubListList(const BFValue& x, const BFValue& y, BFFactory factory) {
  // TODO All calls allowed for this builtin should be optimized away to avoid evaluating here.
  // But this is not yet implemented in DocDB so evaluating inefficiently and in-memory for now.
  // For clarity, this implementation should be removed (see e.g. SubSetSet above) as soon as
  // RemoveFromList is implemented in DocDB.
  BFRetValue result = factory();
  auto& list_elems = *result.mutable_list_value()->mutable_elems();
  if (IsNull(x) || IsNull(y)) {
    return result;
  }

  const auto& xl = x.list_value();
  const auto& yl = y.list_value();
  for (const auto& x_elem : xl.elems()) {
    bool should_remove = false;
    for (const auto& y_elem : yl.elems()) {
      if (x_elem == y_elem) {
        should_remove = true;
        break;
      }
    }
    if (!should_remove) {
      *list_elems.Add() = x_elem;
    }
  }
  return result;
}

//--------------------------------------------------------------------------------------------------
// Now().
inline Result<BFRetValue> NowDate(BFFactory factory) {
  BFRetValue result = factory();
  result.set_date_value(DateTime::DateNow());
  return result;
}


inline Result<BFRetValue> NowTime(BFFactory factory) {
  BFRetValue result = factory();
  result.set_time_value(DateTime::TimeNow());
  return result;
}

inline Result<BFRetValue> NowTimestamp(BFFactory factory) {
  BFRetValue result = factory();
  result.set_timestamp_value(DateTime::TimestampNow().ToInt64());
  return result;
}

//--------------------------------------------------------------------------------------------------
// uuid().
static const uint16_t kUUIDType = 4;
inline Result<BFRetValue> GetUuid(BFFactory factory) {
  auto uuid = Uuid::Generate();
  RSTATUS_DCHECK_EQ(uuid.version(), kUUIDType, IllegalState, "Unexpected UUID type");
  BFRetValue result = factory();
  uuid.ToBytes(result.mutable_uuid_value());
  return result;
}

//--------------------------------------------------------------------------------------------------
// Map::Map
inline Result<BFRetValue> MapConstructor(const BFParams& params, BFFactory factory) {
  BFRetValue result = factory();
  auto* qlmap = result.mutable_map_value();
  RSTATUS_DCHECK(params.size()%2 == 0, RuntimeError, "Unexpected argument count for map::map");
  for (size_t i = 0; i < params.size(); i++) {
    *qlmap->mutable_keys()->Add() = params[i];
    *qlmap->mutable_values()->Add() = params[++i];
  }
  return result;
}

// Set::Set
inline Result<BFRetValue> SetConstructor(const BFParams& params, BFFactory factory) {
  BFRetValue result = factory();
  auto* qlset = result.mutable_set_value();
  for (const auto& param : params) {
    *qlset->mutable_elems()->Add() = param;
  }
  return result;
}

// List::List
inline Result<BFRetValue> ListConstructor(const BFParams& params, BFFactory factory) {
  BFRetValue result = factory();
  auto* qllist = result.mutable_list_value();
  for (const auto& param : params) {
    *qllist->mutable_elems()->Add() = param;
  }
  return result;
}

// Map::Frozen
inline auto MapFromVector(const BFParams& params) {
  std::map<BFCollectionEntry, BFCollectionEntry> ordered_values;
  for (size_t vidx = 0; vidx < params.size(); vidx++) {
    auto kidx = vidx++;
    ordered_values.emplace(std::ref(params[kidx]), std::ref(params[vidx]));
  }
  return ordered_values;
}

inline Result<BFRetValue> MapFrozen(const BFParams& params, BFFactory factory) {
  auto map_elems = MapFromVector(params);

  BFRetValue result = factory();
  auto& elems = *result.mutable_frozen_value()->mutable_elems();
  for (auto& elem : map_elems) {
    *elems.Add() = elem.first;
    *elems.Add() = elem.second;
  }
  return result;
}

// Set::Frozen.
inline auto SetFromVector(const BFParams& params) {
  std::set<BFCollectionEntry> ordered_values;
  for (size_t i = 0; i < params.size(); i++) {
    ordered_values.insert(params[i]);
  }
  return ordered_values;
}

inline Result<BFRetValue> SetFrozen(const BFParams& params, BFFactory factory) {
  auto set_elems = SetFromVector(params);

  BFRetValue result = factory();
  auto& elems = *result.mutable_frozen_value()->mutable_elems();
  for (auto& elem : set_elems) {
    *elems.Add() = elem;
  }
  return result;
}

// List::Frozen.
inline Result<BFRetValue> ListFrozen(const BFParams& params, BFFactory factory) {
  BFRetValue result = factory();
  auto& elems = *result.mutable_frozen_value()->mutable_elems();
  for (const auto& param : params) {
    *elems.Add() = param;
  }
  return result;
}

//--------------------------------------------------------------------------------------------------

} // namespace bfql
} // namespace yb
