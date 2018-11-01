// Copyright (c) YugaByte, Inc.
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

// Implementations of YB C API functions generated from the API DSL. This is compiled with the
// C++ compiler, but exports functions to C.

#include "yb/yql/pggate/ybc_pggate.h"

#include "yb/yql/pggate/pggate.h"

#include "yb/yql/pggate/if_macros_c_wrapper_impl.h"

extern "C" {

#include "yb/yql/pggate/pggate_if.h"

}

// We don't really need this in the end of a file, but include it anyway for consistency.
#include "yb/yql/pggate/if_macros_undef.h"
