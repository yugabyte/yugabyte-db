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

#include "yb/master/master_fwd.h"

#include "yb/rpc/rpc_fwd.h"

namespace yb {
namespace master {

std::unique_ptr<rpc::ServiceIf> MakeMasterAdminService(Master* master);
std::unique_ptr<rpc::ServiceIf> MakeMasterBackupService(Master* master);
std::unique_ptr<rpc::ServiceIf> MakeMasterClientService(Master* master);
std::unique_ptr<rpc::ServiceIf> MakeMasterClusterService(Master* master);
std::unique_ptr<rpc::ServiceIf> MakeMasterDclService(Master* master);
std::unique_ptr<rpc::ServiceIf> MakeMasterDdlService(Master* master);
std::unique_ptr<rpc::ServiceIf> MakeMasterEncryptionService(Master* master);
std::unique_ptr<rpc::ServiceIf> MakeMasterHeartbeatService(Master* master);
std::unique_ptr<rpc::ServiceIf> MakeMasterReplicationService(Master* master);
std::unique_ptr<rpc::ServiceIf> MakeMasterTestService(Master* master);

} // namespace master
} // namespace yb
