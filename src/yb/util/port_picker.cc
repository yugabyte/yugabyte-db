// Copyright (c) YugaByte, Inc.

#include <mutex>

#include "yb/util/port_picker.h"
#include "yb/util/net/net_util.h"

namespace yb {

uint16_t PortPicker::AllocateFreePort() {
  std::lock_guard<std::mutex> lock(mutex_);

  // This will take a file lock ensuring the port does not get claimed by another thread/process
  // and add it to our vector of such locks that will be freed on minicluster shutdown.
  free_port_file_locks_.emplace_back();
  return GetFreePort(&free_port_file_locks_.back());
}

}  // namespace yb
