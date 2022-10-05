// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

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

  Status ReloadKeysAndCertificates() override;
  std::string GetCertificateDetails() override;

 protected:
  Status RegisterServices() override;
  Status SetupMessengerBuilder(rpc::MessengerBuilder* builder) override;

 private:
  std::unique_ptr<rpc::SecureContext> secure_context_;
};

} // namespace enterprise
} // namespace master
} // namespace yb
#endif // ENT_SRC_YB_MASTER_MASTER_H
