//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <atomic>
#include <string>

#include "yb/rocksdb/env.h"
#include "yb/util/strongly_typed_bool.h"

YB_STRONGLY_TYPED_BOOL(CopyFileSync);

namespace yb {
class PriorityThreadPoolSuspender;
}

namespace rocksdb {

// Copy a file up to a specified size. If passed size is 0 - copy the whole file.
// Will return "file too small" error status if `size` is larger than size of the source file.
// If sync is true, the destination file will be synced to disk before returning.
//
// `env_options` supplies the rate limiter, if any, that the write side of the copy should be
// throttled by. `suspender` makes the copy preemptible when it runs as a priority thread pool
// task: the writer checks it every time it flushes its buffer, so a higher priority task queued
// meanwhile takes the worker over instead of waiting for the whole copy to finish.
// `shutting_down`, if given, is polled once per buffer: as soon as it is set the copy stops and
// returns ShutdownInProgress, leaving a partial destination file the caller must remove.
Status CopyFile(
    Env* env, const std::string& source, const std::string& destination, uint64_t size = 0,
    CopyFileSync sync = CopyFileSync::kFalse, const EnvOptions& env_options = EnvOptions(),
    yb::PriorityThreadPoolSuspender* suspender = nullptr,
    const std::atomic<bool>* shutting_down = nullptr);

// Recursively delete the specified directory.
Status DeleteRecursively(Env* env, const std::string& dirname);

// Deletes SST file by `fname`. If `db_options` has a file manager and `path_id` is 0 then
// it schedules file deletion instead of deleting it immediately.
Status DeleteSSTFile(
    const DBOptions* db_options, const std::string& fname, uint32_t path_id);

}  // namespace rocksdb
