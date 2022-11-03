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

#include "yb/util/flags.h"

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/enums.h"

DECLARE_string(cert_node_filename);

namespace yb {

class FsManager;

namespace rpc {

class SecureContext;

}

namespace server {

YB_DEFINE_ENUM(SecureContextType, (kInternal)(kExternal));

std::string DefaultCertsDir(const FsManager& fs_manager);

// Creates secure context and sets up messenger builder to use it.
Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& hosts, const FsManager& fs_manager, SecureContextType type,
    rpc::MessengerBuilder* builder);

Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& root_dir, const std::string& name, SecureContextType type,
    rpc::MessengerBuilder* builder);

Result<std::unique_ptr<rpc::SecureContext>> SetupSecureContext(
    const std::string& cert_dir, const std::string& root_dir, const std::string& name,
    SecureContextType type, rpc::MessengerBuilder* builder);

Result<std::unique_ptr<rpc::SecureContext>> SetupInternalSecureContext(
    const std::string& local_hosts, const FsManager& fs_manager,
    rpc::MessengerBuilder* messenger_builder);

YB_STRONGLY_TYPED_BOOL(UseClientCerts);

Result<std::unique_ptr<rpc::SecureContext>> CreateSecureContext(
    const std::string& certs_dir, UseClientCerts use_client_certs,
    const std::string& node_name = std::string(),
    const std::string& required_uid = std::string());

Status ReloadSecureContextKeysAndCertificates(
    rpc::SecureContext* context, const std::string& root_dir, SecureContextType type,
    const std::string& hosts);

Status ReloadSecureContextKeysAndCertificates(
    rpc::SecureContext* context, const std::string& node_name, const std::string& root_dir,
    SecureContextType type);

Status ReloadSecureContextKeysAndCertificates(
    rpc::SecureContext* context, const std::string& certs_dir, const std::string& node_name);

void ApplySecureContext(const rpc::SecureContext* context, rpc::MessengerBuilder* builder);

bool IsNodeToNodeEncryptionEnabled();

bool IsClientToServerEncryptionEnabled();

} // namespace server
} // namespace yb
