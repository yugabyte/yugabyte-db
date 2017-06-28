// Copyright (c) YugaByte, Inc.

#ifndef YB_UTIL_SHARED_LOCK_MANAGER_FWD_H_
#define YB_UTIL_SHARED_LOCK_MANAGER_FWD_H_

namespace yb {
namespace util {

const size_t NUM_LOCK_TYPES = 6;
enum class LockType {
  SR_READ_WEAK = 0,
  SR_READ_STRONG = 1,
  SR_WRITE_WEAK = 2,
  SR_WRITE_STRONG = 3,
  SI_WRITE_WEAK = 4,
  SI_WRITE_STRONG = 5
};

typedef std::map<std::string, LockType> LockBatch;
typedef uint32_t LockState;

class SharedLockManager;

}  // namespace util
}  // namespace yb

#endif  // YB_UTIL_SHARED_LOCK_MANAGER_FWD_H_
