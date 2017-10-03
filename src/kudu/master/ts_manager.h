// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef KUDU_MASTER_TS_MANAGER_H
#define KUDU_MASTER_TS_MANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "kudu/gutil/macros.h"
#include "kudu/util/locks.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"

namespace kudu {

class NodeInstancePB;

namespace master {

class TSDescriptor;
class TSRegistrationPB;

typedef std::vector<std::shared_ptr<TSDescriptor> > TSDescriptorVector;

// Tracks the servers that the master has heard from, along with their
// last heartbeat, etc.
//
// Note that TSDescriptors are never deleted, even if the TS crashes
// and has not heartbeated in quite a while. This makes it simpler to
// keep references to TSDescriptors elsewhere in the master without
// fear of lifecycle problems. Dead servers are "dead, but not forgotten"
// (they live on in the heart of the master).
//
// This class is thread-safe.
class TSManager {
 public:
  TSManager();
  virtual ~TSManager();

  // Lookup the tablet server descriptor for the given instance identifier.
  // If the TS has never registered, or this instance doesn't match the
  // current instance ID for the TS, then a NotFound status is returned.
  // Otherwise, *desc is set and OK is returned.
  Status LookupTS(const NodeInstancePB& instance,
                  std::shared_ptr<TSDescriptor>* desc);

  // Lookup the tablet server descriptor for the given UUID.
  // Returns false if the TS has never registered.
  // Otherwise, *desc is set and returns true.
  bool LookupTSByUUID(const std::string& uuid,
                        std::shared_ptr<TSDescriptor>* desc);

  // Register or re-register a tablet server with the manager.
  //
  // If successful, *desc reset to the registered descriptor.
  Status RegisterTS(const NodeInstancePB& instance,
                    const TSRegistrationPB& registration,
                    std::shared_ptr<TSDescriptor>* desc);

  // Return all of the currently registered TS descriptors into the provided
  // list.
  void GetAllDescriptors(std::vector<std::shared_ptr<TSDescriptor> >* descs) const;

  // Return all of the currently registered TS descriptors that have sent a
  // heartbeat recently, indicating that they're alive and well.
  void GetAllLiveDescriptors(std::vector<std::shared_ptr<TSDescriptor> >* descs) const;

  // Get the TS count.
  int GetCount() const;

 private:
  mutable rw_spinlock lock_;

  typedef std::unordered_map<
    std::string, std::shared_ptr<TSDescriptor> > TSDescriptorMap;
  TSDescriptorMap servers_by_id_;

  DISALLOW_COPY_AND_ASSIGN(TSManager);
};

} // namespace master
} // namespace kudu

#endif
