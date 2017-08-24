// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_MASTER_H
#define ENT_SRC_YB_MASTER_MASTER_H

#include "../../src/yb/master/master.h"

namespace yb {
namespace master {
namespace enterprise {

class Master : public yb::master::Master {
  typedef yb::master::Master super;
 public:
  explicit Master(const MasterOptions& opts) : super(opts) {}

 protected:
  CHECKED_STATUS RegisterServices() override;

 private:
  DISALLOW_COPY_AND_ASSIGN(Master);
};

} // namespace enterprise
} // namespace master
} // namespace yb
#endif // ENT_SRC_YB_MASTER_MASTER_H
