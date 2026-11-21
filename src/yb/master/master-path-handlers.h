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
#pragma once

#include <sstream>
#include <string>
#include <vector>

#include "yb/common/wire_protocol.pb.h"

#include "yb/server/webserver.h"

#include "yb/util/result.h"

namespace yb::master {

// Pages a master that is not the leader serves without its catalog, given the result of
// ListMasters. Used by the Rust master's C++ host (src/yb/master_host).

// Fetches the page `req` asks for from the master leader.
void RedirectToLeader(
    const Result<std::vector<ServerEntryPB>>& masters,
    const Result<ServerRegistrationPB>& local_reg, const Webserver::WebRequest& req,
    Webserver::WebResponse* resp);
// The /masters table; `local_uuid` is shown in bold.
void RenderMastersPage(
    const Result<std::vector<ServerEntryPB>>& masters, const std::string& local_uuid,
    std::stringstream* output);
// /api/v1/masters.
void RenderMastersJson(
    const Result<std::vector<ServerEntryPB>>& masters, std::stringstream* output);

}  // namespace yb::master
