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

#pragma once

namespace yb::master {

// Overrides the defaults of server flags that differ for a master. Call before parsing gflags so
// that explicit command-line values still win. Shared by the C++ yb-master and the Rust master's
// host (src/yb/master_host) so both start with identical defaults.
void SetMasterFlagDefaults();

}  // namespace yb::master
