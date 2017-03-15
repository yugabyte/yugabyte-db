// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef YB_CLIENT_SCHEMA_INTERNAL_H
#define YB_CLIENT_SCHEMA_INTERNAL_H

#include <string>

#include "yb/client/schema.h"
#include "yb/common/common.pb.h"

namespace yb {
namespace client {

// Helper functions that convert between client-facing and internal PB enums.
class YBColumnSpec::Data {
 public:
  explicit Data(std::string name)
      : name(std::move(name)),
        has_type(false),
        has_order(false),
        sorting_type(ColumnSchema::SortingType::kNotSpecified),
        has_encoding(false),
        has_compression(false),
        has_block_size(false),
        has_nullable(false),
        primary_key(false),
        hash_primary_key(false),
        has_default(false),
        default_val(NULL),
        remove_default(false),
        has_rename_to(false) {
  }

  ~Data() {
    delete default_val;
  }

  const std::string name;

  bool has_type;
  YQLType type;

  bool has_order;
  int32_t order;

  ColumnSchema::SortingType sorting_type;

  bool has_encoding;
  EncodingType encoding;

  bool has_compression;
  CompressionType compression;

  bool has_block_size;
  int32_t block_size;

  bool has_nullable;
  bool nullable;

  bool primary_key;
  bool hash_primary_key;

  bool has_default;
  YBValue* default_val; // Owned.

  // For ALTER
  bool remove_default;

  // For ALTER
  bool has_rename_to;
  std::string rename_to;
};

} // namespace client
} // namespace yb
#endif // YB_CLIENT_SCHEMA_INTERNAL_H
