// Copyright (c) YugaByte, Inc.
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

#include <condition_variable>
#include <memory>
#include <mutex>

#include "yb/client/client.h"
#include "yb/client/stateful_services/pg_auto_analyze_service_client.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/thread_annotations.h"

#include "yb/util/status_fwd.h"
#include "yb/util/thread.h"
#include "yb/util/unique_lock.h"

namespace yb {
namespace tserver {

class TabletServer;

class TableMutationCountSender {
 public:
  explicit TableMutationCountSender(TabletServer* server);
  ~TableMutationCountSender();
  Status Start() EXCLUDES(mutex_);
  Status Stop() EXCLUDES(mutex_);

 private:
  void RunThread() EXCLUDES(mutex_);

  Status DoSendMutationCounts() EXCLUDES(mutex_);

  // The server for which we are sending tables mutation counts.
  TabletServer& server_;

  scoped_refptr<yb::Thread> thread_;

  // Mutex/condition pair to trigger the table mutation count sender thread
  std::mutex mutex_;
  std::condition_variable cond_;

  // Protected by mutex_.
  bool stopped_ GUARDED_BY(mutex_) = false;

  std::unique_ptr<client::PgAutoAnalyzeServiceClient> client_;

  DISALLOW_COPY_AND_ASSIGN(TableMutationCountSender);
};

} // namespace tserver
} // namespace yb
