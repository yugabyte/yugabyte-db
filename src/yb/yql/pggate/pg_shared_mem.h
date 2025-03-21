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

#include <string_view>

#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/shmem/shared_mem_allocator.h"
#include "yb/util/status_fwd.h"

namespace yb::pggate {

void PgSetupSharedMemoryAddressSegment();

void PgBackendSetupSharedMemory();

tserver::SharedMemoryManager& PgSharedMemoryManager();

} // namespace yb::pggate
