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

#include "yb/common/pgsql_protocol.pb.h"

template<class PB>
bool HasSkipIntents(const PB& pb) {
  if constexpr (requires { pb.skip_intents_write(); }) {
    return pb.skip_intents_write();
  } else {
    static_assert(requires { pb.skip_intents_read(); });
    return pb.skip_intents_read();
  }
}
