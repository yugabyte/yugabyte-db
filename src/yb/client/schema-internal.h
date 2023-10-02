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
#pragma once

#include <string>

#include "yb/client/schema.h"

namespace yb {
namespace client {

// Helper functions that convert between client-facing and internal PB enums.
class YBColumnSpec::Data {
 public:
  explicit Data(std::string name)
      : name(std::move(name)) {
  }

  const std::string name;

  QLTypePtr type;

  int32_t order = 0;

  yb::Nullable nullable = Nullable::kTrue;

  ColumnKind kind = ColumnKind::VALUE;

  bool static_column = false;

  bool is_counter = false;

  int32_t pg_type_oid = kPgInvalidOid;

  QLValuePB missing_value;

  // For ALTER
  std::optional<std::string> rename_to;
};

} // namespace client
} // namespace yb
