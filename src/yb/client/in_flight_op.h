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

#include <string>

#include "yb/util/flags.h"

#include "yb/client/client_fwd.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/enums.h"
#include "yb/util/status.h"

namespace yb {
namespace client {
namespace internal {

// An operation which has been submitted to the batcher and not yet completed.
// The operation goes through a state machine as it progresses through the
// various stages of a request. See the State enum for details.
//
// Note that in-flight ops *conceptually* hold a reference to the Batcher object.
// However, since there might be millions of these objects floating around,
// we can save a pointer per object by manually incrementing the Batcher ref-count
// when we create the object, and decrementing when we delete it.
struct InFlightOp {
  InFlightOp(std::shared_ptr<YBOperation> yb_op_, size_t seq_no);

  InFlightOp(const InFlightOp& rhs) = delete;
  void operator=(const InFlightOp& rhs) = delete;
  InFlightOp(InFlightOp&& rhs) = default;
  InFlightOp& operator=(InFlightOp&& rhs) = default;

  // The actual operation.
  std::shared_ptr<YBOperation> yb_op;

  std::string partition_key;

  // The tablet the operation is destined for.
  // This is only filled in after passing through the kLookingUpTablet state.
  RemoteTabletPtr tablet;

  Status error;

  // Each operation has a unique sequence number which preserves the user's intended
  // order of operations. This is important when multiple operations act on the same row.
  size_t sequence_number;

  // Set only for the first operation in group.
  // Operations are groupped by tablet and operation kind (write, leader read, follower read).
  int64_t batch_idx = -1;

  std::string ToString() const;
};

} // namespace internal
} // namespace client
} // namespace yb
