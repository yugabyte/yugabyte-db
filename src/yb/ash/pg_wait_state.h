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

#include "yb/ash/wait_state.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::ash {

// This class is not thread-safe and should be used only in the context of
// a single thread. It is used to track the wait state information for
// PostgreSQL sessions which is single-threaded.
class PgWaitStateInfo : public WaitStateInfo {
 public:
  explicit PgWaitStateInfo(std::reference_wrapper<const YbcPgAshConfig> config);

  AshMetadata metadata() override;

 private:
  const YbcPgAshConfig& config_;
  AshMetadata cached_metadata_;
};

} // namespace yb::ash
