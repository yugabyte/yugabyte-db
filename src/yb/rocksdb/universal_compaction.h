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

#include <climits>

#include "yb/util/size_literals.h"

using yb::operator"" _MB;

namespace rocksdb {

//
// Algorithm used to make a compaction request stop picking new files
// into a single compaction run
//
enum CompactionStopStyle {
  kCompactionStopStyleSimilarSize, // pick files of similar size
  kCompactionStopStyleTotalSize    // total size of picked files > next file
};

class CompactionOptionsUniversal {
 public:

  // Percentage flexibilty while comparing file size. If the candidate file(s)
  // size is 1% smaller than the next file's size, then include next file into
  // this candidate set. // Default: 1
  unsigned int size_ratio;

  // Always include files with size less or equal to always_include_threshold into candidate set.
  size_t always_include_size_threshold = 0;

  // The minimum number of files in a single compaction run. Default: 2
  unsigned int min_merge_width;

  // The maximum number of files in a single compaction run. Default: UINT_MAX
  unsigned int max_merge_width;

  // The size amplification is defined as the amount (in percentage) of
  // additional storage needed to store a single byte of data in the database.
  // For example, a size amplification of 2% means that a database that
  // contains 100 bytes of user-data may occupy upto 102 bytes of
  // physical storage. By this definition, a fully compacted database has
  // a size amplification of 0%. Rocksdb uses the following heuristic
  // to calculate size amplification: it assumes that all files excluding
  // the earliest file contribute to the size amplification.
  // Default: 200, which means that a 100 byte database could require upto
  // 300 bytes of storage.
  unsigned int max_size_amplification_percent;

  // If this option is set to be -1 (the default value), all the output files
  // will follow compression type specified.
  //
  // If this option is not negative, we will try to make sure compressed
  // size is just above this value. In normal cases, at least this percentage
  // of data will be compressed.
  // When we are compacting to a new file, here is the criteria whether
  // it needs to be compressed: assuming here are the list of files sorted
  // by generation time:
  //    A1...An B1...Bm C1...Ct
  // where A1 is the newest and Ct is the oldest, and we are going to compact
  // B1...Bm, we calculate the total size of all the files as total_size, as
  // well as  the total size of C1...Ct as total_C, the compaction output file
  // will be compressed iff
  //   total_C / total_size < this percentage
  // Default: -1
  int compression_size_percent;

  // The algorithm used to stop picking files into a single compaction run
  // Default: kCompactionStopStyleTotalSize
  CompactionStopStyle stop_style;

  // Option to optimize the universal multi level compaction by enabling
  // trivial move for non overlapping files.
  // Default: false
  bool allow_trivial_move;

  // Default set of parameters
  CompactionOptionsUniversal()
      : size_ratio(1),
        min_merge_width(2),
        max_merge_width(UINT_MAX),
        max_size_amplification_percent(200),
        compression_size_percent(-1),
        stop_style(kCompactionStopStyleTotalSize),
        allow_trivial_move(false) {}
};

}  // namespace rocksdb
