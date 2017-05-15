// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// A Status encapsulates the result of an operation.  It may indicate success,
// or it may indicate an error with an associated error message.
//
// Multiple threads can invoke const methods on a Status without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Status must use
// external synchronization.

#ifndef ROCKSDB_INCLUDE_ROCKSDB_STATUS_H
#define ROCKSDB_INCLUDE_ROCKSDB_STATUS_H

#include <string>

#include "yb/util/status.h"

namespace rocksdb {

typedef yb::Status Status;

}  // namespace rocksdb

#endif // ROCKSDB_INCLUDE_ROCKSDB_STATUS_H
