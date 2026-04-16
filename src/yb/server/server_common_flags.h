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

namespace yb {

// Indicates if the YB universe is in the process of a YSQL major version upgrade (e.g., pg11 to
// pg15). This will return true before any process has been upgraded to the new version, and will
// return false after the upgrade has been finalized.
// This will return false for regular YB upgrades (both major and minor).
// DevNote: Finalize is a multi-step process involving YsqlMajorCatalog Finalize, AutoFlag Finalize,
// and YsqlUpgrade. This will return false after the AutoFlag Finalize step.
bool IsYsqlMajorVersionUpgradeInProgress();

}  // namespace yb
