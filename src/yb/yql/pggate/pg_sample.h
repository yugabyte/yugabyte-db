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
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/yql/pggate/pg_select_index.h"

#include "yb/yql/pggate/pg_tools.h"

namespace yb::pggate {

class PgSamplePicker;

//--------------------------------------------------------------------------------------------------
// SAMPLE collect table statistics and take random rows sample
//--------------------------------------------------------------------------------------------------
class PgSample : public PgDmlRead {
 public:
  PgSample(
      PgSession::ScopedRefPtr pg_session, int targrows, const PgObjectId& table_id,
      bool is_region_local);

  StmtOp stmt_op() const override { return StmtOp::STMT_SAMPLE; }

  // Prepare query
  Status Prepare() override;

  // Prepare PgSamplePicker's random state
  Status InitRandomState(double rstate_w, uint64_t rand_state_s0, uint64_t rand_state_s1);

  // Make PgSamplePicker to process next block of rows in the table.
  // The has_more parameter is set to true if table has and needs more blocks.
  // PgSampler is not ready to be executed until this function returns false
  Result<bool> SampleNextBlock();

  // Retrieve estimated number of live and dead rows. Available after execution.
  Result<EstimatedRowCount> GetEstimatedRowCount();

 private:
  Result<PgSamplePicker&> SamplePicker();

  // How many sample rows are needed
  const int targrows_;
};

}  // namespace yb::pggate
