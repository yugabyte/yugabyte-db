// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_PORT_PICKER_H
#define YB_UTIL_PORT_PICKER_H

#include <mutex>
#include <memory>

#include "yb/util/env.h"

namespace yb {

// Allows allocating ports that would be safe to start a server on for the lifetime of this object.
class PortPicker {
 public:
  // Allocates a free port and stores a file lock guarding access to that port into an internal
  // array of file locks.
  uint16_t AllocateFreePort();

 private:
  std::vector<std::unique_ptr<FileLock> > free_port_file_locks_;
  std::mutex mutex_;
};

}  // namespace yb

#endif  // YB_UTIL_PORT_PICKER_H
