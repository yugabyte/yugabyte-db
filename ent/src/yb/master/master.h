// Copyright (c) YugaByte, Inc.

#ifndef ENT_SRC_YB_MASTER_MASTER_H
#define ENT_SRC_YB_MASTER_MASTER_H

namespace yb {
namespace master {
namespace enterprise {

class CatalogManager;

} // namespace enterprise
} // namespace master
} // namespace yb

#include "../../../../src/yb/master/master.h"

namespace yb {

namespace rpc {

class SecureContext;

}

namespace master {
namespace enterprise {

class Master : public yb::master::Master {
  typedef yb::master::Master super;
 public:
  explicit Master(const MasterOptions& opts);
  ~Master();
  Master(const Master&) = delete;
  void operator=(const Master&) = delete;

 protected:
  CHECKED_STATUS RegisterServices() override;
  CHECKED_STATUS SetupMessengerBuilder(rpc::MessengerBuilder* builder) override;

 private:
  std::unique_ptr<rpc::SecureContext> secure_context_;
};

} // namespace enterprise
} // namespace master
} // namespace yb
#endif // ENT_SRC_YB_MASTER_MASTER_H
