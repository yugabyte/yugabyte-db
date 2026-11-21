// Copyright (c) YugabyteDB, Inc.
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

#include <functional>
#include <optional>

#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

class FsManager;
class HostPort;

namespace pgwrapper {
class PgWrapperContext;
}  // namespace pgwrapper

namespace server {
class ServerBaseOptions;
}  // namespace server

namespace master {

// The pg_upgrade step of a YSQL major upgrade: a temporary new-version postgres in binary upgrade
// mode, and pg_upgrade from the postgres of the closest live tserver (`tserver_pg`, its host and
// pg port). For a tserver on this node, `get_pg_socket_dir` asks it for its socket directory;
// nullopt means it is too old to answer. Run by the Rust master's host.
Status PerformYsqlPgUpgrade(
    pgwrapper::PgWrapperContext& server, const server::ServerBaseOptions& options,
    FsManager& fs_manager, HostPort tserver_pg, bool local_tserver,
    const std::function<Result<std::optional<HostPort>>()>& get_pg_socket_dir);

// Removes the pg_upgrade socket directory a killed yb-master could not clean up itself; run at
// startup.
Status CleanupStalePgUpgradeSocketDir();

}  // namespace master
}  // namespace yb
