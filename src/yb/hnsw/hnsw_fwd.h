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

#include <memory>
#include <vector>

#include "yb/gutil/endian.h"

namespace yb::hnsw {

class BlockCache;
class FileBlockCache;

using BlockCachePtr = std::shared_ptr<BlockCache>;
using DataBlock = std::vector<std::byte>;
using FileBlockCachePtr = std::unique_ptr<FileBlockCache>;
using HnswDistanceType = float;
using HnswEndian = LittleEndian;

} // namespace yb::hnsw
