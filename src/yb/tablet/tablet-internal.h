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
#ifndef YB_TABLET_TABLET_INTERNAL_H
#define YB_TABLET_TABLET_INTERNAL_H

// Macro helpers.

// Make sure RocksDB does not disappear while we're using it. This is used at the top level of
// functions that perform RocksDB operations (directly or indirectly). Once a function is using
// this mechanism, any functions that it calls can safely use RocksDB as usual.
#define GUARD_AGAINST_ROCKSDB_SHUTDOWN \
  if (IsShutdownRequested()) { \
    return STATUS(IllegalState, "tablet is shutting down"); \
  } \
  ScopedPendingOperation shutdown_guard(&pending_op_counter_);

#endif  // YB_TABLET_TABLET_INTERNAL_H
