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

#ifndef YB_COMMON_QL_ROWWISE_ITERATOR_INTERFACE_H
#define YB_COMMON_QL_ROWWISE_ITERATOR_INTERFACE_H

#include <memory>

#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

class HybridTime;
class PgsqlReadRequestPB;
class PgsqlResponsePB;
class QLReadRequestPB;
class QLResponsePB;
class QLTableRow;
class Schema;

namespace common {

class YQLRowwiseIteratorIf {
 public:
  typedef std::unique_ptr<common::YQLRowwiseIteratorIf> UniPtr;
  virtual ~YQLRowwiseIteratorIf() {}

  //------------------------------------------------------------------------------------------------
  // Pure virtual API methods.
  //------------------------------------------------------------------------------------------------
  // Checks whether next row exists.
  virtual bool HasNext() const = 0;

  // Skip the current row.
  virtual void SkipRow() = 0;

  // If restart is required returns restart hybrid time, based on iterated records.
  // Otherwise returns invalid hybrid time.
  virtual HybridTime RestartReadHt() = 0;

  virtual std::string ToString() const = 0;

  virtual const Schema& schema() const = 0;

  //------------------------------------------------------------------------------------------------
  // Virtual API methods.
  // These methods are not applied to virtual/system table.
  //------------------------------------------------------------------------------------------------

  // Apache Cassandra Only: CQL supports static columns while all other intefaces do not.
  // Is the next row column to read a static column?
  virtual bool IsNextStaticColumn() const {
    return false;
  }

  // Checks whether we have processed enough rows for a page and sets the appropriate paging
  // state in the response object.
  virtual CHECKED_STATUS SetPagingStateIfNecessary(const QLReadRequestPB& request,
                                                   const size_t num_rows_skipped,
                                                   QLResponsePB* response) const {
    return Status::OK();
  }
  virtual CHECKED_STATUS SetPagingStateIfNecessary(const PgsqlReadRequestPB& request,
                                                   PgsqlResponsePB* response) const {
    return Status::OK();
  }

  virtual Result<std::string> GetRowKey() const {
    return STATUS(NotSupported, "This iterator does not provide row key");
  }

  //------------------------------------------------------------------------------------------------
  // Common API methods.
  //------------------------------------------------------------------------------------------------
  // Read next row using the specified projection.
  CHECKED_STATUS NextRow(const Schema& projection, QLTableRow* table_row) {
    return DoNextRow(projection, table_row);
  }

  CHECKED_STATUS NextRow(QLTableRow* table_row) {
    return DoNextRow(schema(), table_row);
  }

 private:
  virtual CHECKED_STATUS DoNextRow(const Schema& projection, QLTableRow* table_row) = 0;
};

}  // namespace common
}  // namespace yb
#endif // YB_COMMON_QL_ROWWISE_ITERATOR_INTERFACE_H
