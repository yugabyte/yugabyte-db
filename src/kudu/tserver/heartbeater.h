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
#ifndef KUDU_TSERVER_HEARTBEATER_H
#define KUDU_TSERVER_HEARTBEATER_H

#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/util/status.h"

namespace kudu {
namespace tserver {

class TabletServer;
struct TabletServerOptions;

// Component of the Tablet Server which is responsible for heartbeating to the
// leader master.
//
// TODO: send heartbeats to non-leader masters.
class Heartbeater {
 public:
  Heartbeater(const TabletServerOptions& options, TabletServer* server);
  Status Start();
  Status Stop();

  // Trigger a heartbeat as soon as possible, even if the normal
  // heartbeat interval has not expired.
  void TriggerASAP();

  ~Heartbeater();

 private:
  class Thread;
  gscoped_ptr<Thread> thread_;
  DISALLOW_COPY_AND_ASSIGN(Heartbeater);
};

} // namespace tserver
} // namespace kudu
#endif /* KUDU_TSERVER_HEARTBEATER_H */
