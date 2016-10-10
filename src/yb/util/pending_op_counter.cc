// Copyright (c) YugaByte, Inc.

#include "yb/util/pending_op_counter.h"

#include <glog/logging.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/util/monotime.h"
#include "yb/util/status.h"

using strings::Substitute;

namespace yb {
namespace util {

// The implementation is based on TransactionTracker::WaitForAllToFinish.
Status PendingOperationCounter::WaitForAllOpsToFinish(const MonoDelta& timeout) const {
  const int complain_ms = 1000;
  MonoTime start_time = MonoTime::Now(MonoTime::FINE);
  int64_t num_pending_ops = 0;
  int num_complaints = 0;
  int wait_time_usec = 250;
  while ((num_pending_ops = Get()) > 0) {
    const MonoDelta diff = MonoTime::Now(MonoTime::FINE).GetDeltaSince(start_time);
    if (diff.MoreThan(timeout)) {
      return STATUS(TimedOut, Substitute(
          "Timed out waiting for all pending operations to complete. "
          "$0 transactions pending. Waited for $1",
          num_pending_ops, diff.ToString()));
    }
    const int64_t waited_ms = diff.ToMilliseconds();
    if (waited_ms / complain_ms > num_complaints) {
      LOG(WARNING) << Substitute("Waiting for $0 pending operations to complete now for $1 ms",
                                 num_pending_ops, waited_ms);
      num_complaints++;
    }
    wait_time_usec = std::min(wait_time_usec * 5 / 4, 1000000);
    SleepFor(MonoDelta::FromMicroseconds(wait_time_usec));
  }
  CHECK_EQ(num_pending_ops, 0) << "Number of pending operations must be non-negative.";
  return Status::OK();
}

}  // namespace util
}  // namespace yb
