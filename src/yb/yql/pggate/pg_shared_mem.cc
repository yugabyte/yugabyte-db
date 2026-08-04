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

#include "yb/yql/pggate/pg_shared_mem.h"

#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/logging.h"
#include "yb/util/shmem/annotations.h"
#include "yb/util/shmem/reserved_address_segment.h"
#include "yb/util/status.h"

#include "yb/yql/pggate/pggate_flags.h"

namespace yb::pggate {

void PgSetupSharedMemoryAddressSegment() {
  const char* fd_str = getenv("YB_PG_ADDRESS_NEGOTIATOR_FD");
  CHECK(fd_str)
      << "YB_PG_ADDRESS_NEGOTIATOR_FD is not set, cannot perform shared memory address negotiation";
  int fd = std::atoi(fd_str);
  CHECK_OK(PgSharedMemoryManager().InitializePostmaster(fd));

  MarkChildProcess();
}

void PgBackendSetupSharedMemory() {
  CHECK(!FLAGS_pggate_tserver_shared_memory_uuid.empty())
      << "pggate_tserver_shared_memory_uuid not set, not initializing shared memory allocators";
  auto& shared_mem_manager = PgSharedMemoryManager();
  CHECK_OK(shared_mem_manager.InitializePgBackend(FLAGS_pggate_tserver_shared_memory_uuid));
}

tserver::SharedMemoryManager& PgSharedMemoryManager() {
  static tserver::SharedMemoryManager shared_mem_manager;
  return shared_mem_manager;
}

} // namespace yb::pggate
