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
#ifndef YB_MASTER_MASTER_OPTIONS_H
#define YB_MASTER_MASTER_OPTIONS_H

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
  MasterOptions();

  // To be used for testing
  MasterOptions(std::shared_ptr<std::vector<HostPort>> master_addresses, bool is_creating);

  // Need copy constructor as AtomicBool doesnt allow default copy.
  MasterOptions(const MasterOptions& other);

  bool IsDistributed() const { return !master_addresses_->empty(); }

  bool IsClusterCreationMode() const { return is_creating_; }

  bool IsShellMode() const { return is_shell_mode_.Load(); }
  void SetShellMode(bool mode) { is_shell_mode_.Store(mode); }

  void ValidateMasterAddresses() const OVERRIDE;

 protected:
  // Set when its first setup of the cluster - master_addresses is non-empty and is_create is true.
  bool is_creating_;

  // Set during startup of a new master which is not part of any cluster yet - master_addresses is
  // not set and is_create is not set.
  AtomicBool is_shell_mode_;
};

} // namespace master
} // namespace yb
#endif /* YB_MASTER_MASTER_OPTIONS_H */
