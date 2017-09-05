// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <string>
#include "yb/rocksdb/env.h"

namespace rocksdb {

// This API is experimental. We will mark it stable once we run it in production
// for a while.
// NewFlashcacheAwareEnv() creates and Env that blacklists all background
// threads (used for flush and compaction) from using flashcache to cache their
// reads. Reads from compaction thread don't need to be cached because they are
// going to be soon made obsolete (due to nature of compaction)
// Usually you would pass Env::Default() as base.
// cachedev_fd is a file descriptor of the flashcache device. Caller has to
// open flashcache device before calling this API.
extern std::unique_ptr<Env> NewFlashcacheAwareEnv(
    Env* base, const int cachedev_fd);

}  // namespace rocksdb
