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

#include "yb/server/server_base_options.h"
#include "yb/util/atomic.h"

namespace yb {
namespace master {

// Options for constructing the master.
// These are filled in by gflags by default -- see the .cc file for
// the list of options and corresponding flags.
class MasterOptions : public server::ServerBaseOptions {
 public:
  static Result<MasterOptions> CreateMasterOptions();

  explicit MasterOptions(server::MasterAddressesPtr master_addresses);

  // Need copy constructor as AtomicBool doesnt allow default copy.
  MasterOptions(const MasterOptions& other)
      : server::ServerBaseOptions(other), is_shell_mode_(other.IsShellMode()) {}

  MasterOptions(MasterOptions&& other)
      : server::ServerBaseOptions(other), is_shell_mode_(other.IsShellMode()) {}

  // Checks if the master_addresses flags has any peer masters provided.
  // Used to detect 'shell' master startup.
  bool AreMasterAddressesProvided() const {
    return GetMasterAddresses().get()->size() >= 1;
  }

  bool IsShellMode() const { return is_shell_mode_.Load(); }
  void SetShellMode(bool mode) { is_shell_mode_.Store(mode); }

  static const char* kServerType;

 private:
  // Set during startup of a new master which is not part of any cluster yet - master_addresses is
  // not set and there is no local instance file.
  AtomicBool is_shell_mode_{false};
};

} // namespace master
} // namespace yb
