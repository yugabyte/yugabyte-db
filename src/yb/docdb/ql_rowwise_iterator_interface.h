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

#ifndef YB_DOCDB_QL_ROWWISE_ITERATOR_INTERFACE_H
#define YB_DOCDB_QL_ROWWISE_ITERATOR_INTERFACE_H

#include <memory>

#include "yb/common/common_fwd.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {

class Slice;

namespace docdb {

class YQLRowwiseIteratorIf {
 public:
  typedef std::unique_ptr<YQLRowwiseIteratorIf> UniPtr;
  virtual ~YQLRowwiseIteratorIf() {}

  //------------------------------------------------------------------------------------------------
  // Pure virtual API methods.
  //------------------------------------------------------------------------------------------------
  // Checks whether next row exists.
  virtual Result<bool> HasNext() const = 0;

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

  // Retrieves the next key to read after the iterator finishes for the given page.
  virtual CHECKED_STATUS GetNextReadSubDocKey(SubDocKey* sub_doc_key) const;

  // Returns the tuple id of the current tuple. See DocRowwiseIterator for details.
  virtual Result<Slice> GetTupleId() const;

  // Seeks to the given tuple by its id. See DocRowwiseIterator for details.
  virtual Result<bool> SeekTuple(const Slice& tuple_id);

  //------------------------------------------------------------------------------------------------
  // Common API methods.
  //------------------------------------------------------------------------------------------------
  // Read next row using the specified projection.
  CHECKED_STATUS NextRow(const Schema& projection, QLTableRow* table_row);

  CHECKED_STATUS NextRow(QLTableRow* table_row);

 private:
  virtual CHECKED_STATUS DoNextRow(const Schema& projection, QLTableRow* table_row) = 0;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_QL_ROWWISE_ITERATOR_INTERFACE_H
