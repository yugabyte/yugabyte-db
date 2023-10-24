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

#include "yb/common/schema.h"
#include "yb/common/ql_wire_protocol.h"
#include "yb/common/wire_protocol.h"

#include "yb/docdb/schema_packing.h"

namespace yb {
namespace docdb {

struct DocReadContext {
  explicit DocReadContext(const std::string& log_prefix, TableType table_type);

  DocReadContext(
      const std::string& log_prefix, TableType table_type, const Schema& schema_,
      SchemaVersion schema_version);

  DocReadContext(const DocReadContext& rhs, const Schema& schema_, SchemaVersion schema_version);

  DocReadContext(const DocReadContext& rhs, const Schema& schema);

  DocReadContext(const DocReadContext& rhs, SchemaVersion min_schema_version);

  template <class PB>
  Status LoadFromPB(const PB& pb) {
    RETURN_NOT_OK(SchemaFromPB(pb.schema(), &schema));
    RETURN_NOT_OK(schema_packing_storage.LoadFromPB(pb.old_schema_packings()));
    schema_packing_storage.AddSchema(pb.schema_version(), schema);
    LogAfterLoad();
    return Status::OK();
  }

  template <class PB>
  Status MergeWithRestored(const PB& pb, OverwriteSchemaPacking overwrite) {
    RETURN_NOT_OK(schema_packing_storage.MergeWithRestored(
        pb.schema_version(), pb.schema(), pb.old_schema_packings(), overwrite));
    LogAfterMerge(overwrite);
    return Status::OK();
  }

  template <class PB>
  void ToPB(SchemaVersion schema_version, PB* out) {
    DCHECK(schema.has_column_ids());
    SchemaToPB(schema, out->mutable_schema());
    schema_packing_storage.ToPB(schema_version, out->mutable_old_schema_packings());
  }

  // Should account for every field in DocReadContext.
  static bool TEST_Equals(const DocReadContext& lhs, const DocReadContext& rhs) {
    return Schema::TEST_Equals(lhs.schema, rhs.schema) &&
        lhs.schema_packing_storage == rhs.schema_packing_storage;
  }

  static DocReadContext TEST_Create(const Schema& schema) {
    return DocReadContext("TEST: ", TableType::YQL_TABLE_TYPE, schema, 0);
  }

  Schema schema;
  SchemaPackingStorage schema_packing_storage;

 private:
  void LogAfterLoad();
  void LogAfterMerge(OverwriteSchemaPacking overwrite);

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  std::string log_prefix_;
};

} // namespace docdb
} // namespace yb
