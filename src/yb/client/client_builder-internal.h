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
#ifndef YB_CLIENT_CLIENT_BUILDER_INTERNAL_H_
#define YB_CLIENT_CLIENT_BUILDER_INTERNAL_H_

#include <string>
#include <vector>

#include "yb/client/client.h"

namespace yb {

namespace client {

class YBClientBuilder::Data {
 public:
  Data();
  ~Data();

  // This is a REST endpoint from which the list of master hosts and ports can be queried. This
  // takes precedence over both 'master_server_addrs_file_' and 'master_server_addrs_'.
  std::string master_server_endpoint_;

  // This is a file which contains the master addresses string in it. It is periodically reloaded
  // to update the master addresses. This takes precedence over 'master_server_addrs_'.
  std::string master_server_addrs_file_;

  // This vector holds the list of master server addresses. Note that each entry in this vector
  // can either be a single 'host:port' or a comma separated list of 'host1:port1,host2:port2,...'.
  std::vector<std::string> master_server_addrs_;

  int32_t num_reactors_;

  MonoDelta default_admin_operation_timeout_;
  MonoDelta default_rpc_timeout_;

 private:
  DISALLOW_COPY_AND_ASSIGN(Data);
};

}  // namespace client
}  // namespace yb

#endif // YB_CLIENT_CLIENT_BUILDER_INTERNAL_H_
