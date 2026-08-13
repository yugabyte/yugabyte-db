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

#include "yb/client/sharded_request_id_allocator.h"

#include <algorithm>
#include <atomic>

#include "yb/gutil/sysinfo.h"

#include "yb/util/flags.h"

DEFINE_RUNTIME_uint32(client_request_id_shards, 0,
    "Number of shards that the retryable request ids of a client are split into. Each shard has "
    "its own client id and its own min_running_request_id, so more shards mean less contention "
    "between the threads and a smaller blast radius of a request that stays unfinished, but also "
    "more per client state on the server. Zero picks an eighth of the CPUs, which keeps the "
    "threads that share a shard well below the number where it degrades.");

namespace yb::client::internal {

// A shard reaches full throughput while up to 16 threads finish requests through it, and degrades
// beyond that, so an eighth of the CPUs leaves margin for the clients whose threads outnumber
// them. The cap bounds the per client state that the server keeps for the shards.
size_t NumRequestIdShards() {
  if (FLAGS_client_request_id_shards) {
    return FLAGS_client_request_id_shards;
  }
  return std::clamp<size_t>(base::NumCPUs() / 8, 2, 64);
}

size_t RequestIdThreadIndex() {
  static std::atomic<size_t> sequence{0};
  thread_local size_t index = sequence.fetch_add(1);
  return index;
}

} // namespace yb::client::internal
