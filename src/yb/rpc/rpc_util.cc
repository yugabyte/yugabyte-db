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
//

#include "yb/rpc/rpc_util.h"

#include "yb/rpc/messenger.h"

#include "yb/util/size_literals.h"

namespace yb {
namespace rpc {

void MessengerShutdownDeleter::operator()(Messenger* messenger) const {
  messenger->Shutdown();
  delete messenger;
}

Slice GetGlobalSkipBuffer() {
#if (!defined(THREAD_SANITIZER))
  // It is OK to write concurrently into this buffer, since we use it for skipping data in case of
  // hitting memory limits and never read from it.
  static uint8_t global_skip_buffer[1_MB];
#else
  // But for TSAN we use thread_local variant to avoid false positives. We don't use TSAN
  // suppression here because of significant slowdown due to detecting race, preparing report and
  // then suppressing it.
  static thread_local uint8_t global_skip_buffer[1_MB];
#endif // (!defined(THREAD_SANITIZER))

  return Slice(global_skip_buffer, sizeof(global_skip_buffer));
}

}  // namespace rpc
}  // namespace yb
