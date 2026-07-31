// Copyright (c) YugabyteDB, Inc.
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

#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <memory>
#include <string>

#include "yb/docdb/docdb_fwd.h"
#include "yb/util/result.h"
#include "yb/common/hybrid_time.h"
#include "yb/dockv/doc_key.h"
#include "yb/util/enums.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

class Slice;
namespace dockv {
class PgTableRow;
struct ReaderProjection;
}  // namespace dockv
namespace qlexpr {
class QLTableRow;
}  // namespace qlexpr

namespace docdb {

YB_DEFINE_ENUM(ReadKey, (kCurrent)(kNext));

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
  virtual Result<ReadRestartData> GetReadRestartData() = 0;

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
  virtual Result<dockv::SubDocKey> GetSubDocKey(ReadKey read_key = ReadKey::kNext);

  // Returns the tuple id of the current tuple. See DocRowwiseIterator for details.
  virtual Slice GetTupleId() const;

  virtual Slice GetRowKey() const;

  // Seeks to the given tuple by its id. See DocRowwiseIterator for details.
  virtual void SeekTuple(
      Slice tuple_id, UpdateFilterKey update_filter_key = UpdateFilterKey::kTrue);

  // Seeks to first record after specified doc_key_prefix. Also accepts RocksDB-shortened doc key
  // (which could have last byte incremented, see rocksdb::ShortenedIndexBuilder).
  // Requirement is that doc_key_prefix could not be in the middle of rocksdb records belonging
  // to the same DocDB row.
  virtual void SeekToDocKeyPrefix(Slice doc_key_prefix);

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
