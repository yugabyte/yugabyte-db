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

#include <stdint.h>

#include "yb/common/hybrid_time.h"

#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/opid.h"

namespace rocksdb {

// Define all public custom types here.

// Represents a sequence number in a WAL file.
typedef uint64_t SequenceNumber;
#define PRISN PRIu64

using yb::OpId;
using yb::HybridTime;

YB_DEFINE_ENUM(UpdateUserValueType, ((kSmallest, 1))((kLargest, -1)));
YB_DEFINE_ENUM(FrontierModificationMode, (kForce)(kUpdate));

// Specific how key value entries are encoded inside the block.
// See Block and BlockBuilder for more details.
YB_DEFINE_ENUM(
    KeyValueEncodingFormat,
    // <key_shared_prefix_size<key_non_shared_size><value_size><key_non_shared_bytes><value_bytes>
    ((kKeyDeltaEncodingSharedPrefix, 1))
    // Advanced key delta encoding optimized for docdb-specific encoded key structure.
    ((kKeyDeltaEncodingThreeSharedParts, 2))
);

inline std::string KeyValueEncodingFormatToString(KeyValueEncodingFormat encoding_format) {
  switch (encoding_format) {
    case KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix:
      return "shared_prefix";
    case KeyValueEncodingFormat::kKeyDeltaEncodingThreeSharedParts:
      return "three_shared_parts";
  }
  FATAL_INVALID_ENUM_VALUE(KeyValueEncodingFormat, encoding_format);
}

}  //  namespace rocksdb
