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

#include "yb/master/master_fwd.h"

namespace yb::rpc {
class RpcContext;
}
namespace yb::tserver {
class AcquireObjectLockRequestPB;
class AcquireObjectLockResponsePB;
class ReleaseObjectLockRequestPB;
class ReleaseObjectLockResponsePB;
}  // namespace yb::tserver

namespace yb::master {

void LockObject(
    Master* master, CatalogManager* catalog_manager, const tserver::AcquireObjectLockRequestPB* req,
    tserver::AcquireObjectLockResponsePB* resp, rpc::RpcContext rpc);

void UnlockObject(
    Master* master, CatalogManager* catalog_manager, const tserver::ReleaseObjectLockRequestPB* req,
    tserver::ReleaseObjectLockResponsePB* resp, rpc::RpcContext rpc);

}  // namespace yb::master
