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

#pragma once

#include <memory>

#include "yb/rpc/rpc_fwd.h"

namespace yb::tools {

// Return a secure context if needed, otherwise nullptr.
//
// certs_dir replaces --certs_dir_name, --certs_dir and --client_node_name together. Connecting to a
// second universe needs it: those flags name this universe's certificates, and two universes
// provisioned separately have different certificate authorities. A supplied certs_dir is read for
// ca.crt alone, so it does not have to hold client certificates.
Result<std::unique_ptr<rpc::SecureContext>> CreateSecureContextIfNeeded(
    rpc::MessengerBuilder& messenger_builder, const std::string& certs_dir = {});

} // namespace yb::tools
