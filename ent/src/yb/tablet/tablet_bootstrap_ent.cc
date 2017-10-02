// Copyright (c) YugaByte, Inc.

#include "yb/tablet/tablet_bootstrap.h"

#include "yb/consensus/log_anchor_registry.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

namespace yb {
namespace tablet {
namespace enterprise {

using consensus::OperationType;
using consensus::CommitMsg;
using consensus::ReplicateMsg;

Status TabletBootstrap::HandleOperation(OperationType op_type,
                                        ReplicateMsg* replicate,
                                        const CommitMsg* commit) {
  return super::HandleOperation(op_type, replicate, commit);
}

} // namespace enterprise
} // namespace tablet
} // namespace yb
