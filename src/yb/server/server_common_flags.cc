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

// This file contains the gFlags that are common across yb-master and yb-tserver processes.

#include "yb/util/flags.h"

// This autoflag was introduced in commit 80def06f8c19ad7cbc52f41b4be48d158157a418, but it had some
// flaws and the feature required a regular gFlag. As of 27.11.2024 there does not exist an infra to
// remove or demote an autoflag, therefore this flag is going to be a dummy flag. A new gFlag
// ycql_ignore_group_by_error in introduced for the same functionality.
DEFINE_RUNTIME_AUTO_bool(ycql_suppress_group_by_error, kLocalVolatile, true, false,
    "This flag is deprecated, please use ycql_ignore_group_by_error");
