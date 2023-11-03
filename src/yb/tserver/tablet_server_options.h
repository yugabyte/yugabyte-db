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

#include <vector>

#include "yb/encryption/encryption_fwd.h"
#include "yb/server/server_base_options.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/listener.h"

namespace yb {
namespace tserver {

// Options for constructing a tablet server.
// These are filled in by gflags by default -- see the .cc file for
// the list of options and corresponding flags.
//
// This allows tests to easily start miniclusters with different
// tablet servers having different options.
class TabletServerOptions : public yb::server::ServerBaseOptions {
 public:
  static Result<TabletServerOptions> CreateTabletServerOptions();

  static const char* kServerType;

  std::vector<std::shared_ptr<rocksdb::EventListener>> listeners;

 private:
  explicit TabletServerOptions(server::MasterAddressesPtr master_addresses);

  void ValidateMasterAddresses() const;
};

} // namespace tserver
} // namespace yb
