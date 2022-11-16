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
// Parameters for executing a SQL statement.
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/common/common_fwd.h"
#include "yb/common/ql_protocol.pb.h"

#include "yb/util/status_fwd.h"

namespace yb {

struct ReadHybridTime;

namespace ql {

// This class represents the parameters for executing a SQL statement.
class StatementParameters {
 public:
  static const SchemaVersion kUseLatest = 0xFFFFFFFF; // Use the latest prepared schema version.

  // Public types.
  typedef std::unique_ptr<StatementParameters> UniPtr;
  typedef std::unique_ptr<const StatementParameters> UniPtrConst;

  // Constructors
  StatementParameters();
  StatementParameters(const StatementParameters& other);
  virtual ~StatementParameters();

  // Accessor functions for page_size.
  uint64_t page_size() const { return page_size_; }
  void set_page_size(const uint64_t page_size) { page_size_ = page_size; }

  // Set paging state.
  Status SetPagingState(const std::string& paging_state);

  // Write paging state to output.
  void WritePagingState(QLPagingStatePB *output) const { output->CopyFrom(paging_state()); }

  // Accessor functions for paging state fields.
  const std::string& table_id() const { return paging_state().table_id(); }

  const std::string& next_partition_key() const { return paging_state().next_partition_key(); }

  const std::string& next_row_key() const { return paging_state().next_row_key(); }

  int64_t total_num_rows_read() const { return paging_state().total_num_rows_read(); }

  int64_t total_rows_skipped() const { return paging_state().total_rows_skipped(); }

  int64_t next_partition_index() const { return paging_state().next_partition_index(); }

  SchemaVersion schema_version() const {
    return (paging_state_ == nullptr || !paging_state_->has_schema_version()) ?
        kUseLatest : paging_state_->schema_version();
  }

  ReadHybridTime read_time() const;

  // Check if a bind variable is unset. To be overridden by subclasses
  // to return actual bind variables status.
  virtual Result<bool> IsBindVariableUnset(const std::string& name,
                                           int64_t pos) const;

  // Retrieve a bind variable for the execution of the statement. To be overridden by subclasses
  // to return actual bind variables.
  virtual Status GetBindVariable(const std::string& name,
                                         int64_t pos,
                                         const std::shared_ptr<QLType>& type,
                                         QLValue* value) const;

  YBConsistencyLevel yb_consistency_level() const {
    return yb_consistency_level_;
  }

  void set_request_id(uint64_t value) {
    request_id_ = value;
  }

  uint64_t request_id() const {
    return request_id_;
  }

 protected:
  void set_yb_consistency_level(const YBConsistencyLevel yb_consistency_level) {
    yb_consistency_level_ = yb_consistency_level;
  }

 private:
  const QLPagingStatePB& paging_state() const {
    return paging_state_ != nullptr ? *paging_state_ : QLPagingStatePB::default_instance();
  }

  // Limit of the number of rows to return set as page size.
  uint64_t page_size_ = 0;

  // Paging State.
  std::unique_ptr<QLPagingStatePB> paging_state_;

  // Consistency level for YB.
  YBConsistencyLevel yb_consistency_level_;

  // Unique identifier of call that initiated this request.
  uint64_t request_id_ = 0;
};

} // namespace ql
} // namespace yb
