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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_SERVER_SERVER_BASE_OPTIONS_H
#define YB_SERVER_SERVER_BASE_OPTIONS_H

#include <string>
#include <vector>
#include <mutex>

#include "yb/fs/fs_manager.h"
#include "yb/server/webserver_options.h"
#include "yb/server/rpc_server.h"
#include "yb/util/net/net_util.h"

namespace yb {

class Env;

namespace server {

// Options common to both types of servers.
// The subclass constructor should fill these in with defaults from
// server-specific flags.
class ServerBaseOptions {
 public:
  typedef std::shared_ptr<const std::vector<HostPort>> addresses_shared_ptr;

  Env* env;

  // This field is to be used as a path component for all the fs roots by FsManager. For now, we
  // expect it to be either master or tserver and passed in from the respective Server object.
  std::string server_type;

  FsManagerOpts fs_opts;
  RpcServerOptions rpc_opts;
  WebserverOptions webserver_opts;

  std::string dump_info_path;
  std::string dump_info_format;

  int32_t metrics_log_interval_ms;

  std::string placement_cloud;
  std::string placement_region;
  std::string placement_zone;

  std::string placement_uuid;

  std::string master_addresses_flag;

  static Status DetermineMasterAddresses(
      const std::string& master_addresses_flag_name, const std::string& master_addresses_flag,
      uint64_t master_replication_factor, std::vector<HostPort>* master_addresses,
      std::string* master_addresses_resolved_str);

  // This can crash the process if you pass in an invalid list of master addresses!
  void SetMasterAddresses(addresses_shared_ptr master_addresses) {
    CHECK_NOTNULL(master_addresses.get());

    SetMasterAddressesNoValidation(master_addresses);
  }

  addresses_shared_ptr GetMasterAddresses() const;

  ServerBaseOptions(const ServerBaseOptions& options);

 protected:
  ServerBaseOptions();

 private:
  void SetMasterAddressesNoValidation(addresses_shared_ptr master_addresses);

  // List of masters this server is aware of. This will get recreated on a master config change.
  // We should ensure that the vector elements are not individually updated. And the shared pointer
  // will guarantee inconsistent in-transit views of the vector are never seen during/across
  // config changes.
  addresses_shared_ptr master_addresses_;

  // Mutex to avoid concurrent access to the variable above.
  mutable std::mutex master_addresses_mtx_;
};

} // namespace server
} // namespace yb
#endif /* YB_SERVER_SERVER_BASE_OPTIONS_H */
