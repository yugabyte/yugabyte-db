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

#include "yb/master_host/embedded_master.h"

#include <optional>

#include "yb/master/embedded_master_if.h"
#include "yb/master/master_options.h"

#include "yb/master_host/master_host.h"

#include "yb/util/flags.h"
#include "yb/util/status_format.h"

#include "master/server/src/ffi.rs.h"

DECLARE_bool(TEST_rust_master_allow_leadership);

namespace yb::master_host {

namespace {

class InProcessRustMaster : public master::EmbeddedMasterIf {
 public:
  InProcessRustMaster(MasterHost* host, rust::Box<master_rs::EmbeddedRustMaster> rust)
      : host_(host), rust_(std::move(rust)) {}

  ~InProcessRustMaster() override { Shutdown(); }

  static Result<std::unique_ptr<master::EmbeddedMasterIf>> Start(
      const master::MasterOptions& opts) {
    // A cluster of Rust masters must elect one of them, which also needs failure detection on.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_rust_master_allow_leadership) = true;
    try {
      auto host = CreateMasterHostWithOptions(opts);
      host->SetEmbedded();
      // The Rust master owns the host until stop_embedded; host_ stays valid until then.
      auto* raw = host.get();
      return std::make_unique<InProcessRustMaster>(
          raw, master_rs::start_embedded(std::move(host)));
    } catch (const std::exception& e) {
      return STATUS_FORMAT(RuntimeError, "Starting the Rust master: $0", e.what());
    }
  }

  void Shutdown() override {
    if (rust_) {
      master_rs::stop_embedded(std::move(*rust_));
      rust_.reset();
      host_ = nullptr;
    }
  }

  server::RpcAndWebServerBase& server() override { return *CHECK_NOTNULL(host_); }

  master::SysCatalogTable& sys_catalog() override { return CHECK_NOTNULL(host_)->sys_catalog(); }

  const master::MasterOptions& opts() const override { return CHECK_NOTNULL(host_)->opts(); }

  bool IsSysCatalogInitialized() const override {
    return CHECK_NOTNULL(host_)->IsSysCatalogInitialized();
  }

 private:
  MasterHost* host_;
  std::optional<rust::Box<master_rs::EmbeddedRustMaster>> rust_;
};

}  // namespace

void RegisterEmbeddedRustMaster() {
  master::RegisterEmbeddedMasterFactory(&InProcessRustMaster::Start);
}

}  // namespace yb::master_host
