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

#include "yb/client/stateful_services/stateful_service_client_base.h"
#include "yb/tserver/stateful_services/test_echo_service.pb.h"
#include "yb/tserver/stateful_services/test_echo_service.proxy.h"

namespace yb::client {

// TestEchoServiceClient(client::YBClient & yb_client);
DEFINE_STATEFUL_SERVICE_CLIENT(TestEcho, TEST_ECHO,
    GetEcho,
    GetEchoCount);

}  // namespace yb::client
