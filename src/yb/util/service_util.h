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

#pragma once

#include "yb/rpc/rpc_context.h"

#include "yb/util/status_fwd.h"
#include "yb/util/status.h"

// Utility macro to setup error response and return if status is not OK.
#define RPC_STATUS_RETURN_ERROR(s, error, code, context) do { \
    auto s_tmp = (s); \
    if (PREDICT_FALSE(!s_tmp.ok())) { \
      SetupErrorAndRespond(error, s_tmp, code, &(context)); \
      return; \
    } \
  } while (false)

// Utility macro to setup error response and return if status is not OK.
#define CHECK_RESULT_RETURN_RPC_ERROR(error, code, context) \
  do { \
    if (PREDICT_FALSE(!__result.ok())) { \
      SetupErrorAndRespond(error, __result.status(), code, &(context)); \
      return; \
    } \
  } while (false)

#define RPC_VERIFY_RESULT(result, error, code, context) \
  RESULT_CHECKER_HELPER(result, CHECK_RESULT_RETURN_RPC_ERROR(error, code, context))

// Utility macros to perform the appropriate check. If the check fails,
// returns the specified (error) Status, with the given message.
#define RPC_CHECK_OP_AND_RETURN_ERROR(var1, op, var2, s, error, code, context) \
  do { \
    auto v1_tmp = (var1); \
    auto v2_tmp = (var2); \
    if (PREDICT_FALSE(!((v1_tmp)op(v2_tmp)))) { \
      RPC_STATUS_RETURN_ERROR(s, error, code, context); \
    } \
  } while (false)

#define RPC_CHECK_AND_RETURN_ERROR(expr, s, error, code, context) \
  RPC_CHECK_OP_AND_RETURN_ERROR(expr, ==, true, s, error, code, context)
#define RPC_CHECK_EQ_AND_RETURN_ERROR(var1, var2, s, error, code, context) \
  RPC_CHECK_OP_AND_RETURN_ERROR(var1, ==, var2, s, error, code, context)
#define RPC_CHECK_NE_AND_RETURN_ERROR(var1, var2, s, error, code, context) \
  RPC_CHECK_OP_AND_RETURN_ERROR(var1, !=, var2, s, error, code, context)
#define RPC_CHECK_GT_AND_RETURN_ERROR(var1, var2, s, error, code, context) \
  RPC_CHECK_OP_AND_RETURN_ERROR(var1, >, var2, s, error, code, context)
#define RPC_CHECK_GE_AND_RETURN_ERROR(var1, var2, s, error, code, context) \
  RPC_CHECK_OP_AND_RETURN_ERROR(var1, >=, var2, s, error, code, context)
#define RPC_CHECK_LT_AND_RETURN_ERROR(var1, var2, s, error, code, context) \
  RPC_CHECK_OP_AND_RETURN_ERROR(var1, <, var2, s, error, code, context)
#define RPC_CHECK_LE_AND_RETURN_ERROR(var1, var2, s, error, code, context) \
  RPC_CHECK_OP_AND_RETURN_ERROR(var1, <=, var2, s, error, code, context)

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

} // namespace yb
