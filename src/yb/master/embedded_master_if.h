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

// Lets MiniMaster run the Rust master in-process (design D12). The Rust master's host
// (src/yb/master_host) depends on this library, so it registers a factory here rather than
// MiniMaster linking it.

#pragma once

#include <functional>
#include <memory>

#include "yb/master/master_fwd.h"

#include "yb/util/result.h"

namespace yb::server {
class RpcAndWebServerBase;
}  // namespace yb::server

namespace yb::master {

// A Rust master running in this process.
class EmbeddedMasterIf {
 public:
  virtual ~EmbeddedMasterIf() = default;

  // Stops it; it cannot be started again.
  virtual void Shutdown() = 0;

  // Its server: messenger, bound addresses, file system, clock.
  virtual server::RpcAndWebServerBase& server() = 0;

  // Its sys-catalog tablet.
  virtual SysCatalogTable& sys_catalog() = 0;

  // The options it was started with.
  virtual const MasterOptions& opts() const = 0;

  // Whether its sys catalog is open, or shell mode was entered.
  virtual bool IsSysCatalogInitialized() const = 0;
};

using EmbeddedMasterFactory =
    std::function<Result<std::unique_ptr<EmbeddedMasterIf>>(const MasterOptions& opts)>;

// Called by the library that embeds the Rust master.
void RegisterEmbeddedMasterFactory(EmbeddedMasterFactory factory);

// Starts a Rust master; fails if no factory is registered (the build has no Rust master).
Result<std::unique_ptr<EmbeddedMasterIf>> StartEmbeddedMaster(const MasterOptions& opts);

}  // namespace yb::master
