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
// Structure definitions for a Postgres table descriptor.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/ash/wait_state.h"

#include "yb/common/transaction.pb.h"

#include "yb/gutil/macros.h"

struct PgExecParameters;

namespace yb::pggate {

RowMarkType GetRowMarkType(const PgExecParameters* exec_params);

struct Bound {
  uint16_t value;
  bool is_inclusive;
};

// A helper to set the specified wait_event_info in the specified
// MyProc and revert to the previous wait_event_info based on RAII
// when it goes out of scope.
class PgWaitEventWatcher {
 public:
  using Starter = uint32_t (*)(uint32_t wait_event);

  PgWaitEventWatcher(Starter starter, ash::WaitStateCode wait_event);
  ~PgWaitEventWatcher();

 private:
  const Starter starter_;
  const uint32_t prev_wait_event_;

  DISALLOW_COPY_AND_ASSIGN(PgWaitEventWatcher);
};

struct EstimatedRowCount {
  double live;
  double dead;
};

} // namespace yb::pggate
