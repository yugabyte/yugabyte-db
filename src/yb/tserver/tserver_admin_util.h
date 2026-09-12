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

#include <string>

#include "yb/tserver/tserver_admin.pb.h"

#include "yb/util/format.h"

namespace yb::tserver {

// DebugString with sensitive material masked, for the request-log sites on both ends of the
// RPC: the fingerprint key is a secret, and start_key is a raw index DocKey -- indexed
// values must not reach logs or support bundles. Takes a copy on purpose.
inline std::string RedactedDebugString(VerifyUniqueIndexTabletRequestPB req) {
  if (req.has_verification_fingerprint_key()) {
    req.set_verification_fingerprint_key("<redacted>");
  }
  if (req.has_start_key()) {
    req.set_start_key(Format("<$0 bytes>", req.start_key().size()));
  }
  return req.DebugString();
}

}  // namespace yb::tserver
