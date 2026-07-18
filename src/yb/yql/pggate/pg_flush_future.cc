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

#include "yb/yql/pggate/pg_flush_future.h"

#include "yb/ash/wait_state.h"

#include "yb/yql/pggate/pg_doc_metrics.h"
#include "yb/yql/pggate/pg_session.h"

namespace yb::pggate {

FlushFuture::FlushFuture(PerformFuture&& future, PgSession& session, PgDocMetrics& metrics)
    : future_(std::move(future)), session_(&session), metrics_(&metrics) {}

Status FlushFuture::Get() {
  uint64_t duration = 0;
  {
    auto watcher = session_->StartWaitEvent(ash::WaitStateCode::kStorageFlush);
    RETURN_NOT_OK(metrics_->CallWithDuration(
        [this] { return future_.Get(*session_); }, &duration));
  }
  metrics_->FlushRequest(duration);
  return Status::OK();
}

bool FlushFuture::Ready() const { return future_.Ready(); }

} // namespace yb::pggate
