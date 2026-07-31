// Copyright (c) YugabyteDB, Inc.
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
//

#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <memory>

#include "yb/master/catalog_manager.h"
#include "yb/master/encryption_manager.h"
#include "yb/master/master_encryption.service.h"
#include "yb/master/master_service_base.h"
#include "yb/master/master_service_base-internal.h"
#include "yb/master/master.h"

namespace yb {
namespace rpc {
class ServiceIf;
}  // namespace rpc

namespace master {

namespace {

class MasterEncryptionServiceImpl : public MasterServiceBase, public MasterEncryptionIf {
 public:
  explicit MasterEncryptionServiceImpl(Master* master)
      : MasterServiceBase(master), MasterEncryptionIf(master->metric_entity()) {}

  MASTER_SERVICE_IMPL_ON_LEADER_WITH_LOCK(
    CatalogManager,
    (ChangeEncryptionInfo)
    (IsEncryptionEnabled)
    (GetFullUniverseKeyRegistry)
  )

  MASTER_SERVICE_IMPL_ON_ALL_MASTERS(
    EncryptionManager,
    (GetUniverseKeyRegistry)
    (AddUniverseKeys)
    (HasUniverseKeyInMemory)
  )
};

} // namespace

std::unique_ptr<rpc::ServiceIf> MakeMasterEncryptionService(Master* master) {
  return std::make_unique<MasterEncryptionServiceImpl>(master);
}

} // namespace master
} // namespace yb
