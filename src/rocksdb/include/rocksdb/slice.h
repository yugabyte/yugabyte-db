// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// Slice is a simple structure containing a pointer into some external
// storage and a size.  The user of a Slice must ensure that the slice
// is not used after the corresponding external storage has been
// deallocated.
//
// Multiple threads can invoke const methods on a Slice without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Slice must use
// external synchronization.

#ifndef ROCKSDB_INCLUDE_ROCKSDB_SLICE_H
#define ROCKSDB_INCLUDE_ROCKSDB_SLICE_H

#include "yb/util/slice.h"

namespace rocksdb {

typedef yb::Slice Slice;
typedef yb::SliceParts SliceParts;

}  // namespace rocksdb

#endif // ROCKSDB_INCLUDE_ROCKSDB_SLICE_H
