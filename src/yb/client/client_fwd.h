//
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
//

#ifndef YB_CLIENT_CLIENT_FWD_H
#define YB_CLIENT_CLIENT_FWD_H

#include <memory>
#include <vector>

template <class T>
class scoped_refptr;

namespace yb {
namespace client {

class YBClient;
typedef std::shared_ptr<YBClient> YBClientPtr;

class YBTransaction;
typedef std::shared_ptr<YBTransaction> YBTransactionPtr;

class TransactionManager;
class YBOperation;
class YBSchema;
class YBTableName;

namespace internal {

struct InFlightOp;
typedef std::shared_ptr<InFlightOp> InFlightOpPtr;
typedef std::vector<InFlightOpPtr> InFlightOps;

class RemoteTablet;
typedef scoped_refptr<RemoteTablet> RemoteTabletPtr;

class RemoteTabletServer;

class Batcher;
typedef scoped_refptr<Batcher> BatcherPtr;

} // namespace internal

} // namespace client
} // namespace yb

#endif // YB_CLIENT_CLIENT_FWD_H
