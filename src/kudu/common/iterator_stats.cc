// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "kudu/common/iterator_stats.h"

#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/status.h"

namespace kudu {

using std::string;
using strings::Substitute;

IteratorStats::IteratorStats()
    : data_blocks_read_from_disk(0),
      bytes_read_from_disk(0),
      cells_read_from_disk(0) {
}

string IteratorStats::ToString() const {
  return Substitute("data_blocks_read_from_disk=$0 "
                    "bytes_read_from_disk=$1 "
                    "cells_read_from_disk=$2",
                    data_blocks_read_from_disk,
                    bytes_read_from_disk,
                    cells_read_from_disk);
}

void IteratorStats::AddStats(const IteratorStats& other) {
  data_blocks_read_from_disk += other.data_blocks_read_from_disk;
  bytes_read_from_disk += other.bytes_read_from_disk;
  cells_read_from_disk += other.cells_read_from_disk;
  DCheckNonNegative();
}

void IteratorStats::SubtractStats(const IteratorStats& other) {
  data_blocks_read_from_disk -= other.data_blocks_read_from_disk;
  bytes_read_from_disk -= other.bytes_read_from_disk;
  cells_read_from_disk -= other.cells_read_from_disk;
  DCheckNonNegative();
}

void IteratorStats::DCheckNonNegative() const {
  DCHECK_GE(data_blocks_read_from_disk, 0);
  DCHECK_GE(bytes_read_from_disk, 0);
  DCHECK_GE(cells_read_from_disk, 0);
}


} // namespace kudu
