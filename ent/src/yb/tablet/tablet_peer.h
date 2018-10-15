// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_TABLET_TABLET_PEER_H
#define ENT_SRC_YB_TABLET_TABLET_PEER_H

#include "../../../../src/yb/tablet/tablet_peer.h"

namespace yb {
namespace tablet {
namespace enterprise {

class TabletPeer : public yb::tablet::TabletPeer{
  typedef yb::tablet::TabletPeer super;
 public:
  template<class... Args>
  explicit TabletPeer(Args&&... args)
      : super(std::forward<Args>(args)...) {}

 protected:
  std::unique_ptr<Operation> CreateOperation(consensus::ReplicateMsg* replicate_msg) override;

 private:
  DISALLOW_COPY_AND_ASSIGN(TabletPeer);
};

}  // namespace enterprise
}  // namespace tablet
}  // namespace yb

#endif // ENT_SRC_YB_TABLET_TABLET_PEER_H
