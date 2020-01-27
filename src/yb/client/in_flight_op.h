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

#ifndef YB_CLIENT_IN_FLIGHT_OP_H
#define YB_CLIENT_IN_FLIGHT_OP_H

#include "yb/gutil/gscoped_ptr.h"

#include "yb/util/locks.h"
#include "yb/util/enums.h"

namespace yb {
namespace client {

class YBOperation;

namespace internal {

class RemoteTablet;

YB_DEFINE_ENUM(InFlightOpState,
    // Waiting for the MetaCache to determine which tablet ID hosts the row associated
    // with this operation. In the case that the relevant tablet's key range was
    // already cached, this state will be passed through immediately. Otherwise,
    // the op may sit in this state for some amount of time while waiting on the
    // MetaCache to perform an RPC to the master and find the correct tablet.
    //
    // OWNERSHIP: the op is present in the 'ops_' set, and also referenced by the
    // in-flight callback provided to MetaCache.
    (kLookingUpTablet)

    // Once the correct tablet has been determined, and the tablet locations have been
    // refreshed, we are ready to send the operation to the server.
    //
    // In MANUAL_FLUSH mode, the operations wait in this state until Flush has been called.
    //
    // In AUTO_FLUSH_BACKGROUND mode, the operations may wait in this state for one of
    // two reasons:
    //
    //   1) There are already too many outstanding RPCs to the given tablet server.
    //
    //      We restrict the number of concurrent RPCs from one client to a given TS
    //      to achieve better batching and throughput.
    //      TODO: not implemented yet NOLINT
    //
    //   2) Batching delay.
    //
    //      In order to achieve better batching, we do not immediately send a request
    //      to a TS as soon as we have one pending. Instead, we can wait for a configurable
    //      number of milliseconds for more requests to enter the queue for the same TS.
    //      This makes it likely that if a caller simply issues a small number of requests
    //      to the same tablet in AUTO_FLUSH_BACKGROUND mode that we'll batch all of the
    //      requests together in a single RPC.
    //      TODO: not implemented yet NOLINT
    //
    // OWNERSHIP: When the operation is in this state, it is present in the 'ops_' set
    // and also in the 'per_tablet_ops' map.
    (kBufferedToTabletServer)

    // Once the operation has been flushed (either due to explicit Flush() or background flush)
    // it will enter this state.
    //
    // OWNERSHIP: when entering this state, the op is removed from 'per_tablet_ops' map
    // and ownership is transfered to a WriteRPC's 'ops_' vector. The op still
    // remains in the 'ops_' set.
    (kRequestSent));

// An operation which has been submitted to the batcher and not yet completed.
// The operation goes through a state machine as it progresses through the
// various stages of a request. See the State enum for details.
//
// Note that in-flight ops *conceptually* hold a reference to the Batcher object.
// However, since there might be millions of these objects floating around,
// we can save a pointer per object by manually incrementing the Batcher ref-count
// when we create the object, and decrementing when we delete it.
struct InFlightOp {
  explicit InFlightOp(std::shared_ptr<YBOperation> yb_op_);

  // Lock protecting the internal state of the op.
  // This is necessary since callbacks may fire from IO threads
  // concurrent with the user trying to abort/delete the batch.
  // See comment above about lock ordering.
  std::atomic<InFlightOpState> state{InFlightOpState::kLookingUpTablet};

  // The actual operation.
  const std::shared_ptr<YBOperation> yb_op;

  std::string partition_key;

  // The tablet the operation is destined for.
  // This is only filled in after passing through the kLookingUpTablet state.
  scoped_refptr<RemoteTablet> tablet;

  // Each operation has a unique sequence number which preserves the user's intended
  // order of operations. This is important when multiple operations act on the same row.
  int sequence_number_;

  // Set only for the first operation in group.
  // Operations are groupped by tablet and operation kind (write, leader read, follower read).
  int64_t batch_idx = -1;

  std::string ToString() const;
};

} // namespace internal
} // namespace client
} // namespace yb

#endif // YB_CLIENT_IN_FLIGHT_OP_H
