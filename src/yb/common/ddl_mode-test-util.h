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

#include <string>
#include <vector>

namespace yb {

// Object locking, concurrent DDL and transactional DDL are enabled/ disabled together, per the
// cross-flag validators in common_flags.cc, so tests must move all three at once or the daemons
// FATAL at startup. These helpers do that: use_legacy selects the legacy mode, where DDLs neither
// take object locks nor run inside the enclosing transaction block.
//
// DDL savepoint support and the new relation fastpath write in transaction blocks require
// transactional DDL, so the legacy mode turns them off too. The new mode leaves them at their
// default: a test that needs one of them on says so itself, after calling this.
//
// The first overload sets the gflags in-process, for tests that run a mini cluster; the second
// appends the corresponding command line flags, for tests that pass them to an external cluster.
void ToggleDDLMode(bool use_legacy);
void ToggleDDLMode(std::vector<std::string>& flags, bool use_legacy);

} // namespace yb
