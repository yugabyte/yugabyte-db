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


namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------
// SAMPLE collect table statistics and take random rows sample
//--------------------------------------------------------------------------------------------------
class PgSample : public PgDmlRead {
 public:
  PgSample(PgSession::ScopedRefPtr pg_session,
           const int targrows,
           const PgObjectId& table_id,
           bool is_region_local);
  virtual ~PgSample();

  StmtOp stmt_op() const override { return StmtOp::STMT_SAMPLE; }

  // Prepare query
  Status Prepare() override;

  // Prepare PgSamplePicker's random state
  Status InitRandomState(double rstate_w, uint64 rand_state);

  // Make PgSamplePicker to process next block of rows in the table.
  // The has_more parameter is set to true if table has and needs more blocks.
  // PgSampler is not ready to be executed until this function returns false
  Status SampleNextBlock(bool *has_more);

  // Retrieve estimated number of live and dead rows. Available after execution.
  Status GetEstimatedRowCount(double *liverows, double *deadrows);

 private:
  // How many sample rows are needed
  const int targrows_;
};

// Internal class to work as the secondary_index_query_ to select sample tuples.
// Like index, it produces ybctids of random records and outer PgSample fetches them.
// Unlike index, it does not use a secondary index, but scans main table instead.
class PgSamplePicker : public PgSelectIndex {
 public:
  PgSamplePicker(PgSession::ScopedRefPtr pg_session,
                 const PgObjectId& table_id,
                 bool is_region_local);
  virtual ~PgSamplePicker();

  // Prepare picker
  Status Prepare() override;

  // Seed random numbers generator before execution
  Status PrepareSamplingState(int targrows, double rstate_w, uint64 rand_state);

  // Process next block of table rows and update the reservoir with ybctids of randomly selected
  // rows from the block. Returns true if there is another block to process.
  // Reservoir is not finalized until this function returns false.
  Result<bool> ProcessNextBlock();

  // Overrides inherited function returning ybctids of records to fetch.
  // PgSamplePicker::FetchYbctidBatch returns entire reservoir in one batch.
  virtual Result<bool> FetchYbctidBatch(const std::vector<Slice> **ybctids) override;

  // Retrieve estimated number of live and dead rows. Available after execution.
  Status GetEstimatedRowCount(double *liverows, double *deadrows);

 private:
  // The reservoir to keep ybctids of selected sample rows
  std::unique_ptr<std::string[]> reservoir_;
  // If true sampling is completed and ybctids can be collected from the reservoir
  bool reservoir_ready_ = false;
  // Vector of Slices pointing to the values in the reservoir
  std::vector<Slice> ybctids_;
};

}  // namespace pggate
}  // namespace yb
