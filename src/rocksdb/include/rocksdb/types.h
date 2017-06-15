// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef ROCKSDB_INCLUDE_ROCKSDB_TYPES_H
#define ROCKSDB_INCLUDE_ROCKSDB_TYPES_H

#include <stdint.h>

#include "yb/util/opid.h"

namespace rocksdb {

// Define all public custom types here.

// Represents a sequence number in a WAL file.
typedef uint64_t SequenceNumber;
using yb::OpId;

}  //  namespace rocksdb

#endif // ROCKSDB_INCLUDE_ROCKSDB_TYPES_H
