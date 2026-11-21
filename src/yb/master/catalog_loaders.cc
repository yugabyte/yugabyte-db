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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/catalog_loaders.h"

#include "yb/util/flags.h"

DEFINE_UNKNOWN_bool(master_ignore_deleted_on_load, true,
  "Whether the Master should ignore deleted tables & tablets on restart.  "
  "This reduces failover time at the expense of garbage data." );

namespace yb {
namespace master {

bool ShouldLoadObject(const SysTablesEntryPB& metadata) {
  // TODO: We need to properly remove deleted tables.  This can happen async of master loading.
  return !FLAGS_master_ignore_deleted_on_load || metadata.state() != SysTablesEntryPB::DELETED;
}

bool ShouldLoadObject(const SysTabletsEntryPB& pb) {
  return true;
}

bool ShouldLoadObject(const SysNamespaceEntryPB& metadata) {
  return true;
}

}  // namespace master
}  // namespace yb
