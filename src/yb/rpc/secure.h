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

#include <boost/preprocessor.hpp>
#include <boost/preprocessor/arithmetic/dec.hpp>
#include <boost/preprocessor/control/expr_iif.hpp>
#include <boost/preprocessor/control/iif.hpp>
#include <boost/preprocessor/logical/bool.hpp>
#include <boost/preprocessor/punctuation/is_begin_parens.hpp>
#include <boost/preprocessor/repetition/for.hpp>
#include <boost/preprocessor/seq/elem.hpp>
#include <boost/preprocessor/seq/enum.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/seq/size.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/variadic/elem.hpp>
#include <memory>
#include <string>

#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/rpc/secure_stream.h"
#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

namespace rpc {
class MessengerBuilder;

YB_DEFINE_ENUM(SecureContextType, (kInternal)(kExternal));

std::string GetCertsDir(const std::string& root_dir);

// Creates secure context and sets up messenger builder to use it.
Result<std::unique_ptr<SecureContext>> SetupSecureContext(
    const std::string& root_dir, const std::string& name, SecureContextType type,
    MessengerBuilder* builder);

Result<std::unique_ptr<SecureContext>> SetupSecureContext(
    const std::string& cert_dir, const std::string& root_dir, const std::string& name,
    SecureContextType type, MessengerBuilder* builder);

Result<std::unique_ptr<SecureContext>> SetupInternalSecureContext(
    const std::string& local_hosts, const std::string& root_dir,
    MessengerBuilder* messenger_builder);

YB_STRONGLY_TYPED_BOOL(UseClientCerts);

Result<std::unique_ptr<SecureContext>> CreateSecureContext(
    const std::string& certs_dir, UseClientCerts use_client_certs,
    const std::string& node_name = std::string(),
    const std::string& required_uid = std::string());

Status ReloadSecureContextKeysAndCertificates(
    SecureContext* context, const std::string& root_dir, SecureContextType type,
    const std::string& hosts);

Status ReloadSecureContextKeysAndCertificates(
    SecureContext* context, const std::string& node_name, const std::string& root_dir,
    SecureContextType type);

Status ReloadSecureContextKeysAndCertificates(
    SecureContext* context, const std::string& certs_dir, const std::string& node_name);

void ApplySecureContext(const SecureContext* context, MessengerBuilder* builder);

bool IsNodeToNodeEncryptionEnabled();

bool IsClientToServerEncryptionEnabled();

} // namespace rpc
} // namespace yb
