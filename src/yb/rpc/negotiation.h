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
#ifndef YB_RPC_NEGOTIATION_H
#define YB_RPC_NEGOTIATION_H

#include <boost/intrusive_ptr.hpp>

#include "yb/gutil/ref_counted.h"
#include "yb/rpc/rpc_fwd.h"
#include "yb/util/monotime.h"

namespace yb {
namespace rpc {

class YBConnectionContext;

class Negotiation {
 public:
  static void RunNegotiation(ConnectionPtr conn, const MonoTime& deadline);
  static void YBNegotiation(ConnectionPtr conn,
                            YBConnectionContext* context,
                            const MonoTime& deadline);
  static void RedisNegotiation(ConnectionPtr conn, const MonoTime& deadline);
  static void CQLNegotiation(ConnectionPtr conn, const MonoTime& deadline);
 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Negotiation);
};

} // namespace rpc
} // namespace yb
#endif // YB_RPC_NEGOTIATION_H
