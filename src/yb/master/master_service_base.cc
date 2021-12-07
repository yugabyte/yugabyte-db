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
//

#include "yb/master/master_service_base.h"
#include "yb/master/master.h"

namespace yb {
namespace master {

// Available overloaded handlers of different types:

enterprise::CatalogManager* MasterServiceBase::handler(CatalogManager*) {
  return server_->catalog_manager_impl();
}

FlushManager* MasterServiceBase::handler(FlushManager*) {
  return server_->flush_manager();
}

PermissionsManager* MasterServiceBase::handler(PermissionsManager*) {
  return &server_->permissions_manager();
}

EncryptionManager* MasterServiceBase::handler(EncryptionManager*) {
  return &server_->encryption_manager();
}

} // namespace master
} // namespace yb
