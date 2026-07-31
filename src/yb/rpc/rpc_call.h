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

#include <stdint.h>
#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <atomic>
#include <string>

#include "yb/rpc/rpc_fwd.h"
#include "yb/rpc/outbound_data.h"
#include "yb/util/enums.h"

namespace yb {

class Status;

namespace rpc {

YB_DEFINE_ENUM(TransferState, (PENDING)(FINISHED)(ABORTED));

class RpcCall : public OutboundData {
 public:
  RpcCall();

  // This functions is invoked in reactor thread of the appropriate connection, except during
  // reactor shutdown. In case of shutdown all such final calls are sequential. Therefore, this
  // function doesn't require synchronization.
  void Transferred(const Status& status, const ConnectionPtr& conn) override;

  virtual std::string LogPrefix() const {
    return "";
  }

  TransferState transfer_state() const { return transfer_state_.load(std::memory_order_acquire); }

 private:
  virtual void NotifyTransferred(const Status& status, const ConnectionPtr& conn) = 0;

  std::atomic<TransferState> transfer_state_{TransferState::PENDING};
  const uint64_t start_time_ns_;
};

}  // namespace rpc
}  // namespace yb
