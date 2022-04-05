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

#ifndef YB_DOCDB_DOC_READ_CONTEXT_H
#define YB_DOCDB_DOC_READ_CONTEXT_H

#include "yb/common/schema.h"

#include "yb/docdb/packed_row.h"

namespace yb {
namespace docdb {

struct DocReadContext {
  DocReadContext() = default;

  DocReadContext(const Schema& schema_, uint32_t schema_version) : schema(schema_) {
    schema_packing_storage.AddSchema(schema_version, schema_);
  }

  DocReadContext(const DocReadContext& rhs, const Schema& schema_, uint32_t schema_version)
      : schema(schema_), schema_packing_storage(rhs.schema_packing_storage) {
    schema_packing_storage.AddSchema(schema_version, schema_);
  }

  template <class PB>
  CHECKED_STATUS LoadFromPB(const PB& pb) {
    RETURN_NOT_OK(SchemaFromPB(pb.schema(), &schema));
    RETURN_NOT_OK(schema_packing_storage.LoadFromPB(pb.old_schema_packings()));
    schema_packing_storage.AddSchema(pb.schema_version(), schema);
    return Status::OK();
  }

  template <class PB>
  void ToPB(uint32_t schema_version, PB* out) {
    DCHECK(schema.has_column_ids());
    SchemaToPB(schema, out->mutable_schema());
    schema_packing_storage.ToPB(schema_version, out->mutable_old_schema_packings());
  }

  Schema schema;
  SchemaPackingStorage schema_packing_storage;
};

} // namespace docdb
} // namespace yb

#endif // YB_DOCDB_DOC_READ_CONTEXT_H
