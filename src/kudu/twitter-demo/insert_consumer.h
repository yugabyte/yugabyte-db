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
#ifndef KUDU_TWITTER_DEMO_INSERT_CONSUMER_H
#define KUDU_TWITTER_DEMO_INSERT_CONSUMER_H

#include "kudu/twitter-demo/twitter_streamer.h"

#include <string>

#include "kudu/client/callbacks.h"
#include "kudu/client/schema.h"
#include "kudu/client/shared_ptr.h"
#include "kudu/rpc/rpc_controller.h"
#include "kudu/tserver/tserver_service.proxy.h"
#include "kudu/twitter-demo/parser.h"
#include "kudu/util/locks.h"
#include "kudu/util/slice.h"

namespace kudu {
namespace client {
class KuduClient;
class KuduTable;
class KuduSession;
class KuduStatusCallback;
} // namespace client

namespace twitter_demo {

class InsertConsumer;

class FlushCB : public client::KuduStatusCallback {
 public:
  explicit FlushCB(InsertConsumer* consumer);

  virtual ~FlushCB();

  virtual void Run(const Status& status) OVERRIDE;
 private:
  InsertConsumer* consumer_;
};

// Consumer of tweet data which parses the JSON and inserts
// into a remote tablet via RPC.
class InsertConsumer : public TwitterConsumer {
 public:
  explicit InsertConsumer(
    const client::sp::shared_ptr<client::KuduClient> &client);
  ~InsertConsumer();

  Status Init();

  virtual void ConsumeJSON(const Slice& json) OVERRIDE;

 private:
  friend class FlushCB;

  void BatchFinished(const Status& s);

  bool initted_;

  client::KuduSchema schema_;
  FlushCB flush_cb_;
  TwitterEventParser parser_;

  // Reusable object for latest event.
  TwitterEvent event_;

  client::sp::shared_ptr<client::KuduClient> client_;
  client::sp::shared_ptr<client::KuduSession> session_;
  client::sp::shared_ptr<client::KuduTable> table_;

  simple_spinlock lock_;
  bool request_pending_;
};

} // namespace twitter_demo
} // namespace kudu
#endif
