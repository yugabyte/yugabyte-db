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

#include <memory>

#include "yb/common/hybrid_time.h"

#include "yb/util/result.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pg_dml_read.h"
#include "yb/yql/pggate/pg_tools.h"

namespace yb::pggate {

class PgSamplePicker;

//--------------------------------------------------------------------------------------------------
// SAMPLE collect table statistics and take random rows sample
//--------------------------------------------------------------------------------------------------
class PgSample final : public PgStatementLeafBase<PgDmlRead, StmtOp::kSample>  {
 public:
  // Make PgSamplePicker to process next block of rows in the table.
  // The has_more parameter is set to true if table has and needs more blocks.
  // PgSampler is not ready to be executed until this function returns false
  Result<bool> SampleNextBlock();

  // Retrieve estimated number of live and dead rows. Available after execution.
  EstimatedRowCount GetEstimatedRowCount();

  static Result<std::unique_ptr<PgSample>> Make(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id, bool is_region_local,
      int targrows, const SampleRandomState& rand_state, HybridTime read_time);

 private:
  explicit PgSample(const PgSession::ScopedRefPtr& pg_session);

  Status Prepare(
      const PgObjectId& table_id, bool is_region_local, int targrows,
      const SampleRandomState& rand_state, HybridTime read_time);

  PgSamplePicker& SamplePicker();
};

}  // namespace yb::pggate
