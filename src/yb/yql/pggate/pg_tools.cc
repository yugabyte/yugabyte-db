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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_tools.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

DECLARE_uint32(TEST_yb_ash_sleep_at_wait_state_ms);
DECLARE_uint32(TEST_yb_ash_wait_code_to_sleep_at);

namespace yb::pggate {

RowMarkType GetRowMarkType(const PgExecParameters* exec_params) {
  return exec_params && exec_params->rowmark > -1
      ? static_cast<RowMarkType>(exec_params->rowmark)
      : RowMarkType::ROW_MARK_ABSENT;
}

PgWaitEventWatcher::PgWaitEventWatcher(
    Starter starter, ash::WaitStateCode wait_event)
    : starter_(starter),
      prev_wait_event_(starter_(yb::to_underlying(wait_event))) {
  if (PREDICT_FALSE(FLAGS_TEST_yb_ash_wait_code_to_sleep_at == to_underlying(wait_event) &&
      PREDICT_FALSE(FLAGS_TEST_yb_ash_sleep_at_wait_state_ms > 0))) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_yb_ash_sleep_at_wait_state_ms));
  }
}

PgWaitEventWatcher::~PgWaitEventWatcher() {
  starter_(prev_wait_event_);
}

} // namespace yb::pggate
