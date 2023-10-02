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

#pragma once

#include "yb/common/pgsql_protocol.messages.h"

#include "yb/util/enums.h"
#include "yb/util/logging.h"

#include "yb/yql/pggate/ybc_pg_typedefs.h"

namespace yb::pggate {

// An enumeration holding the different table types that requests are classified as.
// This is closely based on RelationType in master/master_types.proto.
// Not reusing the RelationType enum helps us achieve two things:
// 1. There is no build-time dependency of pggate on the master proto.
// 2. Allows for a separation between the metrics collected and their internal representation.
YB_DEFINE_ENUM(TableType, (SYSTEM)(USER)(INDEX));

class PgDocMetrics {
 public:
  explicit PgDocMetrics(YBCPgExecStatsState* state);

  void ReadRequest(TableType relation, uint64_t wait_time);
  void WriteRequest(TableType relation);
  void FlushRequest(uint64_t wait_time);
  void RecordRequestMetrics(const LWPgsqlRequestMetricsPB& metrics);

  // A helper function to compute the wait time of a function
  template <class Functor>
  auto CallWithDuration(
      const Functor& functor, uint64_t* duration, bool use_high_res_timer = true) {
    DurationWatcher watcher(duration, !state_.is_timing_required, use_high_res_timer);
    return functor();
  }

  PgsqlMetricsCaptureType metrics_capture() const {
    return static_cast<PgsqlMetricsCaptureType>(state_.metrics_capture);
  }

 private:
  class DurationWatcher {
   public:
    DurationWatcher(uint64_t* duration, bool use_zero_duration, bool use_high_res_timer);
    ~DurationWatcher();

   private:
    uint64_t* duration_;
    const bool use_zero_duration_;
    const bool use_high_res_timer_;
    const uint64_t start_;

    DISALLOW_COPY_AND_ASSIGN(DurationWatcher);
  };

 private:
  YBCPgExecStatsState& state_;

  DISALLOW_COPY_AND_ASSIGN(PgDocMetrics);
};

} // namespace yb::pggate
