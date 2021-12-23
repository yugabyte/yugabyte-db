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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_CLIENT_SCHEMA_INTERNAL_H
#define YB_CLIENT_SCHEMA_INTERNAL_H

#include <string>

#include "yb/client/schema.h"

namespace yb {
namespace client {

// Helper functions that convert between client-facing and internal PB enums.
class YBColumnSpec::Data {
 public:
  explicit Data(std::string name)
      : name(std::move(name)),
        has_type(false),
        has_order(false),
        sorting_type(SortingType::kNotSpecified),
        has_nullable(false),
        primary_key(false),
        hash_primary_key(false),
        static_column(false),
        is_counter(false),
        has_rename_to(false) {
  }

  ~Data() {
  }

  const std::string name;

  bool has_type;
  std::shared_ptr<QLType> type;

  bool has_order;
  int32_t order;

  SortingType sorting_type;

  bool has_nullable;
  bool nullable;

  bool primary_key;
  bool hash_primary_key;

  bool static_column;

  bool is_counter;

  // For ALTER
  bool has_rename_to;
  std::string rename_to;
};

} // namespace client
} // namespace yb
#endif // YB_CLIENT_SCHEMA_INTERNAL_H
