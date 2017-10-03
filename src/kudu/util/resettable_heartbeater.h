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

#ifndef KUDU_UTIL_RESETTABLE_HEARTBEATER_H_
#define KUDU_UTIL_RESETTABLE_HEARTBEATER_H_

#include <boost/function.hpp>
#include <string>

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"

namespace kudu {
class MonoDelta;
class Status;
class ResettableHeartbeaterThread;

typedef boost::function<Status()> HeartbeatFunction;

// A resettable hearbeater that takes a function and calls
// it to perform a regular heartbeat, unless Reset() is called
// in which case the heartbeater resets the heartbeat period.
// The point is to send "I'm Alive" heartbeats only if no regular
// messages are sent in the same period.
//
// TODO Eventually this should be used instead of the master heartbeater
// as it shares a lot of logic with the exception of the specific master
// stuff (and the fact that it is resettable).
//
// TODO We'll have a lot of these per server, so eventually we need
// to refactor this so that multiple heartbeaters share something like
// java's ScheduledExecutor.
//
// TODO Do something about failed hearbeats, right now this is just
// logging. Probably could take more arguments and do more of an
// exponential backoff.
//
// This class is thread safe.
class ResettableHeartbeater {
 public:
  ResettableHeartbeater(const std::string& name,
                        MonoDelta period,
                        HeartbeatFunction function);

  // Starts the heartbeater
  Status Start();

  // Stops the hearbeater
  Status Stop();

  // Resets the heartbeat period.
  // When this is called, the subsequent heartbeat has some built-in jitter and
  // may trigger before a full period (as specified to the constructor).
  void Reset();

  ~ResettableHeartbeater();
 private:
  gscoped_ptr<ResettableHeartbeaterThread> thread_;

  DISALLOW_COPY_AND_ASSIGN(ResettableHeartbeater);
};

}  // namespace kudu

#endif /* KUDU_UTIL_RESETTABLE_HEARTBEATER_H_ */
