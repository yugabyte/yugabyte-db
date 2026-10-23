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

#include <string_view>

#include "opentelemetry/nostd/shared_ptr.h"
#include "opentelemetry/trace/span.h"

#include "yb/tserver/pg_client.pb.h"

#include "yb/util/enums.h"
#include "yb/util/status.h"

namespace yb::tserver {

// Name shared by the outbound (pggate) and inbound (tserver) spans of a shared-memory exchange
// request; both sides must use the same name for traces to pair.
inline std::string_view GetSharedMemSpanName(PgSharedExchangeReqType req_type) {
  switch (req_type) {
    case PgSharedExchangeReqType::PERFORM:
      return "shmem yb.tserver.PgClientService.Perform";
    case PgSharedExchangeReqType::ACQUIRE_OBJECT_LOCK:
      return "shmem yb.tserver.PgClientService.AcquireObjectLock";
    case PgSharedExchangeReqType_INT_MIN_SENTINEL_DO_NOT_USE_: [[fallthrough]];
    case PgSharedExchangeReqType_INT_MAX_SENTINEL_DO_NOT_USE_: break;
  }
  FATAL_INVALID_ENUM_VALUE(PgSharedExchangeReqType, req_type);
}

// Ends a shared-memory exchange span with the request's status and resets the pointer.
inline void EndSharedMemSpan(
    opentelemetry::nostd::shared_ptr<opentelemetry::trace::Span>* span, const Status& status) {
  if (!*span) {
    return;
  }
  if (status.ok()) {
    (*span)->SetStatus(opentelemetry::trace::StatusCode::kOk);
  } else {
    (*span)->SetStatus(opentelemetry::trace::StatusCode::kError, status.ToUserMessage());
  }
  (*span)->End();
  *span = nullptr;
}

}  // namespace yb::tserver
