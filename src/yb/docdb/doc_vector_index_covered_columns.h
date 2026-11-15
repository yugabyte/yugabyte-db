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

#include <memory>
#include <vector>

#include "yb/common/common_fwd.h"
#include "yb/common/schema.h"

#include "yb/docdb/docdb_fwd.h"

#include "yb/dockv/packed_row.h"
#include "yb/dockv/schema_packing.h"

#include "yb/util/kv_util.h"

namespace yb::docdb {

// The covered columns of a vector index: the INCLUDE columns whose values are stored in the payload
// attached to each vector, see PgVectorIdxOptionsPB::covered_column_ids. Their values are packed as
// a row V2 against a packing of their own, without the packed row type and schema version.
class DocVectorIndexCoveredColumns {
 public:
  explicit DocVectorIndexCoveredColumns(Schema schema);

  // Returns nullptr when the index has no covered columns.
  static Result<DocVectorIndexCoveredColumnsPtr> Make(
      const Schema& indexed_table_schema, const PgVectorIdxOptionsPB& options);

  // Covered column ids, in the order the packer expects them.
  const std::vector<ColumnId>& column_ids() const {
    return column_ids_;
  }

  bool Contains(ColumnId column_id) const;

  // The packer has no size limit, so every value is packed.
  dockv::RowPackerV2 MakePacker() const;

  // Completes the packer, returning the covered values to store in the payload.
  static Result<Slice> Complete(dockv::RowPackerV2& packer);

  // Decodes the covered columns that are in the projection of row.
  Status Decode(Slice covered, dockv::PgTableRow& row) const;

 private:
  Schema schema_;
  dockv::SchemaPacking packing_;
  std::vector<ColumnId> column_ids_;
};

} // namespace yb::docdb
