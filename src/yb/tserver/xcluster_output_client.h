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

#include "yb/tserver/xcluster_output_client_interface.h"
#include "yb/cdc/cdc_util.h"

#include "yb/client/client_fwd.h"
#include "yb/rpc/rpc_fwd.h"

#pragma once

namespace rocksdb {

class RateLimiter;

}

namespace yb {

class ThreadPool;

namespace tserver {

class XClusterConsumer;
struct XClusterClient;

std::shared_ptr<XClusterOutputClientIf> CreateXClusterOutputClient(
    XClusterConsumer* xcluster_consumer,
    const cdc::ConsumerTabletInfo& consumer_tablet_info,
    const cdc::ProducerTabletInfo& producer_tablet_info,
    const std::shared_ptr<XClusterClient>& local_client,
    ThreadPool* thread_pool,
    rpc::Rpcs* rpcs,
    std::function<void(const XClusterOutputClientResponse& response)>
        apply_changes_clbk,
    bool use_local_tserver,
    rocksdb::RateLimiter* rate_limiter);

} // namespace tserver
} // namespace yb
