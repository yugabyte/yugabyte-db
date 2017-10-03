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

#ifndef KUDU_SERVER_CLOCK_H_
#define KUDU_SERVER_CLOCK_H_

#include <string>

#include "kudu/common/common.pb.h"
#include "kudu/common/timestamp.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"

namespace kudu {
class faststring;
class MetricEntity;
class MonoDelta;
class Slice;
class Status;
namespace server {

// An interface for a clock that can be used to assign timestamps to
// operations.
// Implementations must respect the following assumptions:
// 1 - Now() must return monotonically increasing numbers
//     i.e. for any two calls, i.e. Now returns timestamp1 and timestamp2, it must
//     hold that timestamp1 < timestamp2.
// 2 - Update() must never set the clock backwards (corollary of 1)
class Clock : public RefCountedThreadSafe<Clock> {
 public:

  // Initializes the clock.
  virtual Status Init() = 0;

  // Obtains a new transaction timestamp corresponding to the current instant.
  virtual Timestamp Now() = 0;

  // Obtains a new transaction timestamp corresponding to the current instant
  // plus the max_error.
  virtual Timestamp NowLatest() = 0;

  // Obtain a timestamp which is guaranteed to be later than the current time
  // on any machine in the cluster.
  //
  // NOTE: this is not a very tight bound.
  virtual Status GetGlobalLatest(Timestamp* t) {
    return Status::NotSupported("clock does not support global properties");
  }

  // Indicates whether this clock supports the required external consistency mode.
  virtual bool SupportsExternalConsistencyMode(ExternalConsistencyMode mode) = 0;

  // Update the clock with a transaction timestamp originating from
  // another server. For instance replicas can call this so that,
  // if elected leader, they are guaranteed to generate timestamps
  // higher than the timestamp of the last transaction accepted from the
  // leader.
  virtual Status Update(const Timestamp& to_update) = 0;

  // Waits until the clock on all machines has advanced past 'then'.
  // Can also be used to implement 'external consistency' in the same sense as
  // Google's Spanner.
  virtual Status WaitUntilAfter(const Timestamp& then,
                                const MonoTime& deadline) = 0;

  // Waits until the clock on this machine advances past 'then'. Unlike
  // WaitUntilAfter(), this does not make any global guarantees.
  virtual Status WaitUntilAfterLocally(const Timestamp& then,
                                       const MonoTime& deadline) = 0;

  // Return true if the given time has definitely passed (i.e any future call
  // to Now() would return a higher value than t).
  virtual bool IsAfter(Timestamp t) = 0;

  // Register the clock metrics in the given entity.
  virtual void RegisterMetrics(const scoped_refptr<MetricEntity>& metric_entity) = 0;

  // Strigifies the provided timestamp according to this clock's internal format.
  virtual std::string Stringify(Timestamp timestamp) = 0;

  virtual ~Clock() {}
};

} // namespace server
} // namespace kudu

#endif /* KUDU_SERVER_CLOCK_H_ */
