// Copyright (c) YugaByte, Inc.
#include "yb/tablet/tablet_peer.h"

namespace yb {
namespace tablet {
namespace enterprise {

std::unique_ptr<Operation> TabletPeer::CreateOperation(consensus::ReplicateMsg* replicate_msg) {
  return super::CreateOperation(replicate_msg);
}

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb
