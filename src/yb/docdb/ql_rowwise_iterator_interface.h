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

#pragma once

#include <memory>

#include <boost/optional.hpp>

#include "yb/common/common_fwd.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {

class Slice;

namespace docdb {

class YQLRowwiseIteratorIf {
 public:
  typedef std::unique_ptr<YQLRowwiseIteratorIf> UniPtr;
  virtual ~YQLRowwiseIteratorIf() = default;

  //------------------------------------------------------------------------------------------------
  // Pure virtual API methods.
  //------------------------------------------------------------------------------------------------
  // Checks whether next row exists.
  Result<bool> FetchNext(
      qlexpr::QLTableRow* table_row,
      const dockv::ReaderProjection* projection = nullptr,
      qlexpr::QLTableRow* static_row = nullptr,
      const dockv::ReaderProjection* static_projection = nullptr) {
    return DoFetchNext(table_row, projection, static_row, static_projection);
  }

  virtual Result<bool> PgFetchNext(dockv::PgTableRow* table_row) = 0;

  // If restart is required returns restart hybrid time, based on iterated records.
  // Otherwise returns invalid hybrid time.
  virtual Result<HybridTime> RestartReadHt() = 0;

  // Returns max seen hybrid time. Only used by tests for validation.
  virtual HybridTime TEST_MaxSeenHt();

  virtual std::string ToString() const = 0;

  //------------------------------------------------------------------------------------------------
  // Virtual API methods.
  // These methods are not applied to virtual/system table.
  //------------------------------------------------------------------------------------------------

  // Apache Cassandra Only: CQL supports static columns while all other intefaces do not.
  // Is the next row column to read a static column?
  virtual bool IsFetchedRowStatic() const {
    return false;
  }

  // Retrieves the next key to read after the iterator finishes for the given page.
  virtual Status GetNextReadSubDocKey(dockv::SubDocKey* sub_doc_key);

  // Returns the tuple id of the current tuple. See DocRowwiseIterator for details.
  virtual Slice GetTupleId() const;

  // Seeks to the given tuple by its id. See DocRowwiseIterator for details.
  virtual void SeekTuple(Slice tuple_id);

  virtual Result<bool> FetchTuple(Slice tuple_id, qlexpr::QLTableRow* row);

 protected:
  virtual Result<bool> DoFetchNext(
      qlexpr::QLTableRow* table_row,
      const dockv::ReaderProjection* projection,
      qlexpr::QLTableRow* static_row,
      const dockv::ReaderProjection* static_projection) = 0;
};

}  // namespace docdb
}  // namespace yb
