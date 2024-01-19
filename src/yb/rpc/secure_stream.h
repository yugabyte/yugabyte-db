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

#include <boost/version.hpp>

#include "yb/rpc/rpc_fwd.h"

#include "yb/util/enums.h"
#include "yb/util/mem_tracker.h"

namespace yb {

class Subprocess;

namespace rpc {

YB_STRONGLY_TYPED_BOOL(MatchingCertKeyPair);
YB_STRONGLY_TYPED_BOOL(RequireClientCertificate);
YB_STRONGLY_TYPED_BOOL(UseClientCertificate);

class SecureContext {
 public:
  SecureContext(RequireClientCertificate require_client_certificate,
                UseClientCertificate use_client_certificate,
                const std::string& required_uid = {});
  ~SecureContext();

  Status AddCertificateAuthorityFile(const std::string& file);

  Status UseCertificates(
      const std::string& ca_cert_file, const Slice& certificate_data, const Slice& pkey_data);

  std::string GetCertificateDetails();

  // Generates and uses temporary keys, should be used only during testing.
  Status TEST_GenerateKeys(int bits, const std::string& common_name,
                           MatchingCertKeyPair matching_cert_key_pair);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;

  friend class SecureRefiner;
};

const Protocol* SecureStreamProtocol();
StreamFactoryPtr SecureStreamFactory(
    StreamFactoryPtr lower_layer_factory, const MemTrackerPtr& buffer_tracker,
    const SecureContext* context);

void InitOpenSSL();

void SetOpenSSLEnv(Subprocess* proc);

bool AllowInsecureConnections();

std::string GetSSLProtocols();
std::string GetCipherList();
std::string GetCipherSuites();

} // namespace rpc
} // namespace yb
