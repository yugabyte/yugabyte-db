// Copyright (c) YugaByte, Inc.

#ifndef YB_COMMON_MASTER_UTIL_H
#define YB_COMMON_MASTER_UTIL_H

#include <memory>

#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/master/master.pb.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status.h"

// This file contains utility functions that can be shared between client and master code.

namespace yb {

class MasterUtil {
 public:
  // Given a hostport, return the master server information protobuf.
  // Does not apply to tablet server.
  static CHECKED_STATUS GetMasterEntryForHost(const std::shared_ptr<rpc::Messenger>& messenger,
                                      const HostPort& hostport,
                                      int timeout,
                                      ServerEntryPB* e);

 private:
  MasterUtil();

  DISALLOW_COPY_AND_ASSIGN(MasterUtil);
};

} // namespace yb

#endif
