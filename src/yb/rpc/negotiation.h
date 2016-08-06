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

#include "yb/gutil/ref_counted.h"
#include "yb/util/monotime.h"

namespace yb {
namespace rpc {

class Connection;
class YBConnection;
class RedisConnection;

class Negotiation {
 public:
  static void RunNegotiation(const scoped_refptr<Connection>& conn,
                             const MonoTime& deadline);
  static void YBNegotiation(const scoped_refptr<YBConnection>& conn,
                            const MonoTime& deadline);
  static void RedisNegotiation(const scoped_refptr<RedisConnection>& conn,
                               const MonoTime& deadline);
 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(Negotiation);
};

} // namespace rpc
} // namespace yb
#endif // YB_RPC_NEGOTIATION_H
