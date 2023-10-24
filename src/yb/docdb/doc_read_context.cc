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

#include "yb/docdb/doc_read_context.h"

#include "yb/util/logging.h"

namespace yb::docdb {

DocReadContext::DocReadContext(const std::string& log_prefix)
    : log_prefix_(log_prefix) {
}

DocReadContext::DocReadContext(const std::string& log_prefix, const Schema& schema_,
                               SchemaVersion schema_version)
    : schema(schema_), log_prefix_(log_prefix) {
  schema_packing_storage.AddSchema(schema_version, schema_);
  LOG_IF_WITH_PREFIX(INFO, schema_version != 0)
      << "DocReadContext, from schema, version: " << schema_version;
}

DocReadContext::DocReadContext(
    const DocReadContext& rhs, const Schema& schema_, SchemaVersion schema_version)
    : schema(schema_), schema_packing_storage(rhs.schema_packing_storage),
      log_prefix_(rhs.log_prefix_) {
  schema_packing_storage.AddSchema(schema_version, schema_);
  LOG_WITH_PREFIX(INFO)
      << "DocReadContext, copy and add: " << schema_packing_storage.VersionsToString()
      << ", added: " << schema_version;
}

DocReadContext::DocReadContext(const DocReadContext& rhs, const Schema& schema_)
    : schema(schema_), schema_packing_storage(rhs.schema_packing_storage),
      log_prefix_(rhs.log_prefix_) {
  LOG_WITH_PREFIX(INFO) << "DocReadContext, copy and replace schema";
}

DocReadContext::DocReadContext(const DocReadContext& rhs, SchemaVersion min_schema_version)
    : schema(rhs.schema), schema_packing_storage(rhs.schema_packing_storage, min_schema_version),
      log_prefix_(rhs.log_prefix_) {
  LOG_WITH_PREFIX(INFO)
      << "DocReadContext, copy and filter: " << rhs.schema_packing_storage.VersionsToString()
      << " => " << schema_packing_storage.VersionsToString() << ", min_schema_version: "
      << min_schema_version;
}

void DocReadContext::LogAfterLoad() {
  LOG_WITH_PREFIX(INFO) << __func__ << ": " << schema_packing_storage.VersionsToString();
}

void DocReadContext::LogAfterMerge(OverwriteSchemaPacking overwrite) {
  LOG_WITH_PREFIX(INFO)
      << __func__ << ": " << schema_packing_storage.VersionsToString() << ", overwrite: "
      << overwrite;
}

} // namespace yb::docdb
