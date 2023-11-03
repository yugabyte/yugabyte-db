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

#pragma once

#include "yb/common/pgsql_protocol.pb.h"

#include "yb/util/status_fwd.h"
#include "yb/util/status_ec.h"
#include "yb/util/yb_pg_errcodes.h"

namespace yb {

struct PgsqlErrorTag : IntegralErrorTag<YBPgErrorCode> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 6;

  static std::string ToMessage(Value value) {
    return ToString(value);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }

};

typedef StatusErrorCodeImpl<PgsqlErrorTag> PgsqlError;

struct PgsqlRequestStatusTag : IntegralErrorTag<PgsqlResponsePB::RequestStatus> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 17;

  static std::string ToMessage(Value value) {
    return PgsqlResponsePB::RequestStatus_Name(value);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }

};

typedef StatusErrorCodeImpl<PgsqlRequestStatusTag> PgsqlRequestStatus;

struct OpIndexTag : IntegralErrorTag<size_t> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 18;

  static std::string ToMessage(Value value) {
    return std::to_string(value);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }

};

typedef StatusErrorCodeImpl<OpIndexTag> OpIndex;

struct RelationOidTag : IntegralErrorTag<unsigned int> {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 20;

  static std::string ToMessage(Value value) {
    return std::to_string(value);
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }
};

typedef StatusErrorCodeImpl<RelationOidTag> RelationOid;

struct AuxilaryMessageTag : StringBackedErrorTag {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 21;

  static std::string ToMessage(const Value& value) {
    return value;
  }

  static std::string DecodeToString(const uint8_t* source) {
    return ToMessage(Decode(source));
  }
};

typedef StatusErrorCodeImpl<AuxilaryMessageTag> AuxilaryMessage;

struct PgsqlMessageArgsTag : StringVectorBackedErrorTag {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 22;

  static std::string ToMessage(const Value& value) {
    return Format("Pgsql Message Arguments: $0", value);
  }
};

typedef yb::StatusErrorCodeImpl<PgsqlMessageArgsTag> PgsqlMessageArgs;

struct FuncNameTag : StringBackedErrorTag {
  // It is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 23;

  static std::string ToMessage(const Value& value) {
    return Format("Function: $0", value);
  }
};

typedef yb::StatusErrorCodeImpl<FuncNameTag> FuncName;

} // namespace yb
