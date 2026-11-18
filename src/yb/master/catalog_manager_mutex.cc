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

#include "yb/master/catalog_manager_mutex.h"

#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_string(catalog_manager_mutex_type, "spinlock",
    "Lock type for the catalog manager's locks: \"spinlock\" (rw_spinlock, waiters poll), "
    "\"rwmutex\" (writer-preferring pthread rwlock) or \"absl\" (absl::Mutex).");
DEFINE_validator(catalog_manager_mutex_type, FLAG_IN_SET_VALIDATOR("spinlock", "rwmutex", "absl"));
TAG_FLAG(catalog_manager_mutex_type, advanced);

namespace yb::master {

namespace {

CatalogManagerMutex::Type TypeFromFlag() {
  if (FLAGS_catalog_manager_mutex_type == "rwmutex") {
    return CatalogManagerMutex::Type::kRWMutex;
  }
  if (FLAGS_catalog_manager_mutex_type == "absl") {
    return CatalogManagerMutex::Type::kAbsl;
  }
  return CatalogManagerMutex::Type::kSpinlock;
}

} // namespace

CatalogManagerMutex::CatalogManagerMutex() : type_(TypeFromFlag()) {}

} // namespace yb::master
