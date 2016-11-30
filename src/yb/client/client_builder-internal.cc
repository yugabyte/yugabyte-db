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

#include "yb/client/client_builder-internal.h"

DEFINE_int32(
    yb_client_num_reactors, 4,
    "Number of reactor threads for the yb client to communicate with different tservers.");

namespace yb {

namespace client {

YBClientBuilder::Data::Data()
    : num_reactors_(FLAGS_yb_client_num_reactors),
      default_admin_operation_timeout_(MonoDelta::FromSeconds(60)),
      default_rpc_timeout_(MonoDelta::FromSeconds(60)) {}

YBClientBuilder::Data::~Data() {
}

}  // namespace client
}  // namespace yb
