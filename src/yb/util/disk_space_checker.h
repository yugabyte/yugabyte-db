//
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

#include <atomic>
#include <shared_mutex>
#include <string>

#include "yb/util/monotime.h"

namespace yb {

class Env;

// Tracks whether the filesystem holding a given directory has enough free space to accept writes.
// The filesystem is queried at most once every --reject_writes_min_disk_space_check_interval_sec
// (more frequently when running low on space); all other calls return the cached result, which
// makes this cheap enough to be called on the write path. Always reports sufficient space when
// --reject_writes_when_disk_full is disabled. Thread safe.
class DiskSpaceChecker {
 public:
  // If always_check_disk is set, every call queries the filesystem instead of returning a cached
  // result. Use it only for infrequent callers that cannot tolerate a stale answer.
  DiskSpaceChecker(Env* env, std::string path, bool always_check_disk = false);

  bool HasSufficientDiskSpace();

  const std::string& path() const { return path_; }

 private:
  Env* const env_;
  const std::string path_;
  const bool always_check_disk_;

  std::atomic<CoarseTimePoint> last_check_time_{CoarseTimePoint::min()};
  std::atomic<bool> has_free_space_{true};
  std::atomic<uint32_t> frequent_check_interval_sec_{0};
  std::shared_timed_mutex mutex_;
};

}  // namespace yb
