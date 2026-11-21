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

#include "yb/master/embedded_master_if.h"

#include <mutex>

#include "yb/util/status_format.h"

namespace yb::master {

namespace {

std::mutex factory_mutex;
EmbeddedMasterFactory& Factory() {
  static auto* factory = new EmbeddedMasterFactory();
  return *factory;
}

}  // namespace

void RegisterEmbeddedMasterFactory(EmbeddedMasterFactory factory) {
  std::lock_guard lock(factory_mutex);
  Factory() = std::move(factory);
}

Result<std::unique_ptr<EmbeddedMasterIf>> StartEmbeddedMaster(const MasterOptions& opts) {
  EmbeddedMasterFactory factory;
  {
    std::lock_guard lock(factory_mutex);
    factory = Factory();
  }
  SCHECK(factory, NotSupported, "This build has no Rust master to embed");
  return factory(opts);
}

}  // namespace yb::master
