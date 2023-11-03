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

#include "yb/common/constants.h"

namespace yb {
namespace master {

static const char* const kSysCatalogTabletId = "00000000000000000000000000000000";
static const char* const kSysCatalogTableId = kObsoleteShortPrimaryTableId;
static const char* const kSysCatalogTableName = "sys.catalog";
static const char* const kSysCatalogTableColType = "entry_type";
static const char* const kSysCatalogTableColId = "entry_id";
static const char* const kSysCatalogTableColMetadata = "metadata";

static const char* const kDbOidColumnName = "db_oid";
static const char* const kCurrentVersionColumnName = "current_version";
static const char* const kLastBreakingVersionColumnName = "last_breaking_version";

static const char* const kDefaultCassandraUsername = "cassandra";
static const char* const kDefaultCassandraPassword = "cassandra";

constexpr uint32_t kSysCatalogSchemaVersion = 0;

}  // namespace master
}  // namespace yb
