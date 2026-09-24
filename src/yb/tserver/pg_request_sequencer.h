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

#include <functional>
#include <memory>
#include <optional>
#include <ostream>

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb::tserver {

class PgClientSession;
class PgSessionGuard;

class PgRequestSequencer {
 public:
  class LogPrefix {
   public:
    explicit LogPrefix(uint64_t session_id) : session_id_(session_id) {}
    friend std::ostream& operator<<(std::ostream&, LogPrefix prefix);

   private:
    uint64_t session_id_;
  };

  using Executor = std::function<void(const Result<PgClientSession&>&)>;

  struct SequenceNum {
    using SerialNo = uint64_t;

    explicit SequenceNum(SerialNo serial_no_, std::optional<SerialNo> predecessor_serial_no_ = {})
        : serial_no(serial_no_), predecessor_serial_no(predecessor_serial_no_) {}

    SerialNo serial_no;
    std::optional<SerialNo> predecessor_serial_no{};
  };

  PgRequestSequencer(CoarseDuration wait_duration, LogPrefix log_prefix);
  ~PgRequestSequencer();

  Result<bool> TryRegisterForProcessing(SequenceNum seq_num);
  Status RegisterForProcessing(
      SequenceNum seq_num, PgSessionGuard& guard, CoarseTimePoint deadline);
  Status Enqueue(SequenceNum seq_num, Executor&& executor, CoarseTimePoint deadline);

  void ProcessPending(PgClientSession& session);

  void Shutdown();

  [[nodiscard]] bool IsProcessingRequiredRegularCheck() const {
    return DoIsProcessingRequired(true);
  }

  [[nodiscard]] bool IsProcessingRequired() const {
    return DoIsProcessingRequired(false);
  }

 private:
  [[nodiscard]] bool DoIsProcessingRequired(bool is_regular_check) const;
  class Impl;

  std::unique_ptr<Impl> impl_;
};

} // namespace yb::tserver
