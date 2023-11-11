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

#pragma once

#include "yb/common/entity_ids_types.h"

#include "yb/tablet/tablet_fwd.h"

namespace yb {
namespace cdc {

class CDCServiceContext {
 public:
  // Lookup the given tablet peer by its ID. Returns nullptr if the tablet is not found.
  virtual tablet::TabletPeerPtr LookupTablet(const TabletId& tablet_id) const = 0;

  // Lookup the given tablet peer by its ID.
  // Returns NotFound error if the tablet is not found.
  virtual Result<tablet::TabletPeerPtr> GetTablet(const TabletId& tablet_id) const = 0;

  // Lookup the given tablet peer by its ID.
  // Returns NotFound error if the tablet is not found.
  // Returns IllegalState if the tablet cannot serve requests.
  virtual Result<tablet::TabletPeerPtr> GetServingTablet(const TabletId& tablet_id) const = 0;

  // Returns permanent UUID of this instance.
  virtual const std::string& permanent_uuid() const = 0;

  // Creates async client initializer with given name and default timeout.
  virtual std::unique_ptr<client::AsyncClientInitializer> MakeClientInitializer(
      const std::string& client_name, MonoDelta default_timeout) const = 0;

  virtual ~CDCServiceContext() = default;
};

} // namespace cdc
} // namespace yb
