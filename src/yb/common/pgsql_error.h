// Copyright (c) YugabyteDB, Inc.
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

#include <string>
#include <string_view>

#include "yb/common/pgsql_protocol.pb.h"

#include "yb/util/status_fwd.h"
#include "yb/util/status_ec.h"
#include "yb/util/tostring.h"
#include "yb/util/yb_pg_errcodes.h"

using namespace std::literals;

namespace yb {

struct PgsqlErrorTag : IntegralErrorTag<YBPgErrorCode> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{6, "pgsql error"sv};

  static std::string ToMessage(Value value) {
    return ToString(value);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }
};

using PgsqlError = StatusErrorCodeImpl<PgsqlErrorTag>;

struct PgsqlRequestStatusTag : IntegralErrorTag<PgsqlResponsePB::RequestStatus> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{17, "pgsql request status"sv};

  static std::string ToMessage(Value value) {
    return PgsqlResponsePB::RequestStatus_Name(value);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }
};

using PgsqlRequestStatus = StatusErrorCodeImpl<PgsqlRequestStatusTag>;

struct OpIndexTag : IntegralErrorTag<size_t> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{18, "op index"sv};

  static std::string ToMessage(Value value) {
    return std::to_string(value);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }
};

using OpIndex = StatusErrorCodeImpl<OpIndexTag>;

struct RelationOidTag : IntegralErrorTag<unsigned int> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{20, "relation oid"sv};

  static std::string ToMessage(Value value) {
    return std::to_string(value);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }
};

using RelationOid = StatusErrorCodeImpl<RelationOidTag>;

struct AuxilaryMessageTag : StringBackedErrorTag {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{21, "aux msg"sv};

  static std::string ToMessage(const Value& value) {
    return value;
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }
};

using AuxilaryMessage = StatusErrorCodeImpl<AuxilaryMessageTag>;

struct PgsqlMessageArgsTag : StringVectorBackedErrorTag {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{22, "pgsql msg args"sv};

  static std::string ToMessage(const Value& value) {
    return Format("Pgsql Message Arguments: $0", value);
  }
};

using PgsqlMessageArgs = yb::StatusErrorCodeImpl<PgsqlMessageArgsTag>;

struct FuncNameTag : StringBackedErrorTag {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr CategoryDescriptor kCategory{23, "function name"sv};

  static std::string ToMessage(const Value& value) {
    return Format("Function: $0", value);
  }
};

using FuncName = yb::StatusErrorCodeImpl<FuncNameTag>;

} // namespace yb
