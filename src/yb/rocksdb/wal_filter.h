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

namespace rocksdb {

class WriteBatch;

// WALFilter allows an application to inspect write-ahead-log (WAL)
// records or modify their processing on recovery.
// Please see the details below.
class WalFilter {
 public:
  enum class WalProcessingOption {
    // Continue processing as usual
    kContinueProcessing = 0,
    // Ignore the current record but continue processing of log(s)
    kIgnoreCurrentRecord = 1,
    // Stop replay of logs and discard logs
    // Logs won't be replayed on subsequent recovery
    kStopReplay = 2,
    // Corrupted record detected by filter
    kCorruptedRecord = 3,
    // Marker for enum count
    kWalProcessingOptionMax = 4
  };

  virtual ~WalFilter() {}

  // LogRecord is invoked for each log record encountered for all the logs
  // during replay on logs on recovery. This method can be used to:
  //  * inspect the record (using the batch parameter)
  //  * ignoring current record
  //    (by returning WalProcessingOption::kIgnoreCurrentRecord)
  //  * reporting corrupted record
  //    (by returning WalProcessingOption::kCorruptedRecord)
  //  * stop log replay
  //    (by returning kStop replay) - please note that this implies
  //    discarding the logs from current record onwards.
  //
  // @params batch          batch encountered in the log during recovery
  // @params new_batch      new_batch to populate if filter wants to change
  //                        the batch (for example to filter some records out,
  //                        or alter some records).
  //                        Please note that the new batch MUST NOT contain
  //                        more records than original, else recovery would
  //                        be failed.
  // @params batch_changed  Whether batch was changed by the filter.
  //                        It must be set to true if new_batch was populated,
  //                        else new_batch has no effect.
  // @returns               Processing option for the current record.
  //                        Please see WalProcessingOption enum above for
  //                        details.
  virtual WalProcessingOption LogRecord(const WriteBatch& batch,
                                        WriteBatch* new_batch,
                                        bool* batch_changed) const = 0;

  // Returns a name that identifies this WAL filter.
  // The name will be printed to LOG file on start up for diagnosis.
  virtual const char* Name() const = 0;
};

}  // namespace rocksdb
