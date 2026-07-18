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

#pragma once

#include <google/protobuf/message.h>

#include "yb/master/master_types.pb.h"

#include "yb/util/result.h"
#include "yb/util/slice.h"

namespace yb::master {

// Returns an empty catalog entity protobuf that maps to the provided SysRowEntryType.
Result<std::unique_ptr<google::protobuf::Message>> CatalogEntityPBForType(SysRowEntryType type);

// Converts a debug string (produced by pb.DebugString(), or pb.ShortDebugString()) into a catalog
// entity protobuf of the given type.
Result<std::unique_ptr<google::protobuf::Message>> DebugStringToCatalogEntityPB(
    SysRowEntryType type, const std::string& debug_string);

// Converts a slice into a catalog entity protobuf of the given type.
Result<std::unique_ptr<google::protobuf::Message>> SliceToCatalogEntityPB(
    SysRowEntryType type, const Slice& data);

}  // namespace yb::master
