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

namespace yb::pggate {

// Helper class to control the code path between PgSession and PgTxnManager components.
// In case the codeflow is going to call the PgTxnManager::SetupPerformOptions method it has to call
// PgSession's method (which builds the SetupPerformOptionsAccessorTag object) first.
// This guarantees that PgSession will be able to make all required preparations prior to the
// PgTxnManager::SetupPerformOptions method call.
class SetupPerformOptionsAccessorTag {
  SetupPerformOptionsAccessorTag() = default;

  friend class PgSession;
};

} // namespace yb::pggate
