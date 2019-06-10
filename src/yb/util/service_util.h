// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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
// Utilities to be used for RPC services.

#ifndef YB_UTIL_SERVICE_UTIL_H_
#define YB_UTIL_SERVICE_UTIL_H_

#include "yb/common/wire_protocol.h"
#include "yb/rpc/rpc_context.h"
#include "yb/util/status.h"


namespace yb {

template <class ErrType, typename ErrEnum>
void SetupErrorAndRespond(ErrType* error,
                          const Status& s,
                          ErrEnum code,
                          rpc::RpcContext* context) {
  // Generic "service unavailable" errors will cause the client to retry later.
  if (code == ErrType::UNKNOWN_ERROR && s.IsServiceUnavailable()) {
    context->RespondRpcFailure(rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY, s);
    return;
  }

  StatusToPB(s, error->mutable_status());
  error->set_code(code);
  context->RespondSuccess();
}

template <class ErrType>
void SetupErrorAndRespond(ErrType* error,
                          const Status& s,
                          rpc::RpcContext* context) {
  SetupErrorAndRespond(error, s, static_cast<typename ErrType::Code>(s.error_code()), context);
}

} // namespace yb


#endif  // YB_UTIL_SERVICE_UTIL_H_
