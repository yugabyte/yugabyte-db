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

#include <stdlib.h>

#include "yb/cdc/cdc_consumer_util.h"
#include "yb/cdc/cdc_output_client_interface.h"

#ifndef ENT_SRC_YB_TSERVER_CDC_POLLER_H
#define ENT_SRC_YB_TSERVER_CDC_POLLER_H

namespace yb {

class ThreadPool;

namespace rpc {

class RpcController;

} // namespace rpc

namespace cdc {

class CDCServiceProxy;

} // namespace cdc

namespace tserver {
namespace enterprise {

class CDCConsumer;


class CDCPoller {
 public:
  CDCPoller(const cdc::ProducerTabletInfo& producer_tablet_info,
            const cdc::ConsumerTabletInfo& consumer_tablet_info,
            std::function<bool(void)> should_continue_polling,
            std::function<cdc::CDCServiceProxy*(void)> get_proxy,
            std::function<void(void)> remove_self_from_pollers_map,
            ThreadPool* thread_pool);

  // Begins poll process for a producer tablet.
  void Poll();
 private:
  void DoPoll();
  // Async handler for Poll.
  void HandlePoll();
  // Does the work of sending the changes to the output client.
  void DoHandlePoll();
  // Async handler for the response from output client.
  void HandleApplyChanges(cdc::OutputClientResponse response);
  // Does the work of polling for new changes.
  void DoHandleApplyChanges(cdc::OutputClientResponse response);

  cdc::ProducerTabletInfo producer_tablet_info_;
  cdc::ConsumerTabletInfo consumer_tablet_info_;
  std::function<bool()> should_continue_polling_;
  std::function<cdc::CDCServiceProxy*(void)> get_proxy_;
  std::function<void(void)> remove_self_from_pollers_map_;

  consensus::OpId op_id_;
  std::unique_ptr<cdc::GetChangesResponsePB> resp_;
  std::unique_ptr<rpc::RpcController> rpc_;
  std::unique_ptr<cdc::CDCOutputClient> output_client_;

  ThreadPool* thread_pool_;

  std::atomic<bool> is_polling_{true};
};

} // namespace enterprise
} // namespace tserver
} // namespace yb

#endif // ENT_SRC_YB_TSERVER_CDC_POLLER_H
