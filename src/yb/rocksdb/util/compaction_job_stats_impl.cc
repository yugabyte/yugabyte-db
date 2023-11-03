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

#include "yb/rocksdb/compaction_job_stats.h"

namespace rocksdb {


void CompactionJobStats::Reset() {
  elapsed_micros = 0;

  num_input_records = 0;
  num_input_files = 0;
  num_input_files_at_output_level = 0;

  num_output_records = 0;
  num_output_files = 0;

  is_manual_compaction = 0;

  total_input_bytes = 0;
  total_output_bytes = 0;

  num_records_replaced = 0;

  total_input_raw_key_bytes = 0;
  total_input_raw_value_bytes = 0;

  num_input_deletion_records = 0;
  num_expired_deletion_records = 0;

  num_corrupt_keys = 0;

  file_write_nanos = 0;
  file_range_sync_nanos = 0;
  file_fsync_nanos = 0;
  file_prepare_write_nanos = 0;
}

void CompactionJobStats::Add(const CompactionJobStats& stats) {
  elapsed_micros += stats.elapsed_micros;

  num_input_records += stats.num_input_records;
  num_input_files += stats.num_input_files;
  num_input_files_at_output_level += stats.num_input_files_at_output_level;

  num_output_records += stats.num_output_records;
  num_output_files += stats.num_output_files;

  total_input_bytes += stats.total_input_bytes;
  total_output_bytes += stats.total_output_bytes;

  num_records_replaced += stats.num_records_replaced;

  total_input_raw_key_bytes += stats.total_input_raw_key_bytes;
  total_input_raw_value_bytes += stats.total_input_raw_value_bytes;

  num_input_deletion_records += stats.num_input_deletion_records;
  num_expired_deletion_records += stats.num_expired_deletion_records;

  num_corrupt_keys += stats.num_corrupt_keys;

  file_write_nanos += stats.file_write_nanos;
  file_range_sync_nanos += stats.file_range_sync_nanos;
  file_fsync_nanos += stats.file_fsync_nanos;
  file_prepare_write_nanos += stats.file_prepare_write_nanos;
}


}  // namespace rocksdb
