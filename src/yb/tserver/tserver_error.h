//
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

#pragma once

#include "yb/tserver/tserver_types.pb.h"

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus_error.h"

#include "yb/tserver/tserver_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/status_ec.h"

namespace yb::tserver {

struct TabletServerErrorTag : IntegralErrorTag<TabletServerErrorPB::Code> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 5;

  typedef TabletServerErrorPB::Code Code;

  static const std::string& ToMessage(Code code) {
    return TabletServerErrorPB::Code_Name(code);
  }
};

typedef StatusErrorCodeImpl<TabletServerErrorTag> TabletServerError;

class MonoDeltaTraits {
 public:
  typedef MonoDelta ValueType;
  typedef int64_t RepresentationType;

  static MonoDelta FromRepresentation(RepresentationType source) {
    return MonoDelta::FromNanoseconds(source);
  }

  static RepresentationType ToRepresentation(MonoDelta value) {
    return value.ToNanoseconds();
  }

  static std::string ToString(MonoDelta value) {
    return value.ToString();
  }
};

struct TabletServerDelayTag : IntegralBackedErrorTag<MonoDeltaTraits> {
  // This category id is part of the wire protocol and should not be changed once released.
  static constexpr uint8_t kCategory = 8;

  static std::string ToMessage(MonoDelta value) {
    return value.ToString();
  }
};

typedef StatusErrorCodeImpl<TabletServerDelayTag> TabletServerDelay;

void SetupError(TabletServerErrorPB* error, const Status& s);

void SetupError(LWTabletServerErrorPB* error, const Status& s);

} // namespace yb::tserver
