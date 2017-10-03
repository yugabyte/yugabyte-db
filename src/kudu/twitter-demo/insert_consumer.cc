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

#include "kudu/twitter-demo/insert_consumer.h"

#include <boost/thread/locks.hpp>
#include <glog/logging.h>
#include <string>
#include <time.h>
#include <vector>

#include "kudu/common/wire_protocol.h"
#include "kudu/common/row.h"
#include "kudu/common/schema.h"
#include "kudu/client/client.h"
#include "kudu/gutil/bind.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/tserver/tserver_service.proxy.h"
#include "kudu/tserver/tserver.pb.h"
#include "kudu/twitter-demo/parser.h"
#include "kudu/twitter-demo/twitter-schema.h"
#include "kudu/util/status.h"

namespace kudu {
namespace twitter_demo {

using tserver::TabletServerServiceProxy;
using tserver::WriteRequestPB;
using tserver::WriteResponsePB;
using rpc::RpcController;
using kudu::client::KuduInsert;
using kudu::client::KuduClient;
using kudu::client::KuduSession;
using kudu::client::KuduStatusCallback;
using kudu::client::KuduTable;
using kudu::client::KuduTableCreator;

FlushCB::FlushCB(InsertConsumer* consumer)
  : consumer_(consumer) {
}

FlushCB::~FlushCB() {
}

void FlushCB::Run(const Status& status) {
  consumer_->BatchFinished(status);
}

InsertConsumer::InsertConsumer(const client::sp::shared_ptr<KuduClient> &client)
  : initted_(false),
    schema_(CreateTwitterSchema()),
    flush_cb_(this),
    client_(client),
    request_pending_(false) {
}

Status InsertConsumer::Init() {
  const char *kTableName = "twitter";
  Status s = client_->OpenTable(kTableName, &table_);
  if (s.IsNotFound()) {
    gscoped_ptr<KuduTableCreator> table_creator(client_->NewTableCreator());
    RETURN_NOT_OK_PREPEND(table_creator->table_name(kTableName)
                          .schema(&schema_)
                          .Create(),
                          "Couldn't create twitter table");
    s = client_->OpenTable(kTableName, &table_);
  }
  RETURN_NOT_OK_PREPEND(s, "Couldn't open twitter table");

  session_ = client_->NewSession();
  session_->SetTimeoutMillis(1000);
  CHECK_OK(session_->SetFlushMode(KuduSession::MANUAL_FLUSH));
  initted_ = true;
  return Status::OK();
}

InsertConsumer::~InsertConsumer() {
  // TODO: to be safe, we probably need to cancel any current RPC,
  // or else the callback will get called on the destroyed object.
  // Given this is just demo code, cutting this corner.
  CHECK(!request_pending_);
}

void InsertConsumer::BatchFinished(const Status& s) {
  boost::lock_guard<simple_spinlock> l(lock_);
  request_pending_ = false;
  if (!s.ok()) {
    bool overflow;
    vector<client::KuduError*> errors;
    ElementDeleter d(&errors);
    session_->GetPendingErrors(&errors, &overflow);
    for (const client::KuduError* error : errors) {
      LOG(WARNING) << "Failed to insert row " << error->failed_op().ToString()
                   << ": " << error->status().ToString();
    }
  }
}

void InsertConsumer::ConsumeJSON(const Slice& json_slice) {
  CHECK(initted_);
  string json = json_slice.ToString();
  Status s = parser_.Parse(json, &event_);
  if (!s.ok()) {
    LOG(WARNING) << "Unable to parse JSON string: " << json << ": " << s.ToString();
    return;
  }

  if (event_.type == DELETE_TWEET) {
    // Not currently supported.
    return;
  }

  string created_at = TwitterEventParser::ReformatTime(event_.tweet_event.created_at);

  gscoped_ptr<KuduInsert> ins(table_->NewInsert());
  KuduPartialRow* r = ins->mutable_row();
  CHECK_OK(r->SetInt64("tweet_id", event_.tweet_event.tweet_id));
  CHECK_OK(r->SetStringCopy("text", event_.tweet_event.text));
  CHECK_OK(r->SetStringCopy("source", event_.tweet_event.source));
  CHECK_OK(r->SetStringCopy("created_at", created_at));
  CHECK_OK(r->SetInt64("user_id", event_.tweet_event.user_id));
  CHECK_OK(r->SetStringCopy("user_name", event_.tweet_event.user_name));
  CHECK_OK(r->SetStringCopy("user_description", event_.tweet_event.user_description));
  CHECK_OK(r->SetStringCopy("user_location", event_.tweet_event.user_location));
  CHECK_OK(r->SetInt32("user_followers_count", event_.tweet_event.user_followers_count));
  CHECK_OK(r->SetInt32("user_friends_count", event_.tweet_event.user_friends_count));
  CHECK_OK(r->SetStringCopy("user_image_url", event_.tweet_event.user_image_url));
  CHECK_OK(session_->Apply(ins.release()));

  // TODO: once the auto-flush mode is implemented, switch to using that
  // instead of the manual batching here
  bool do_flush = false;
  {
    boost::lock_guard<simple_spinlock> l(lock_);
    if (!request_pending_) {
      request_pending_ = true;
      do_flush = true;
    }
  }
  if (do_flush) {
    VLOG(1) << "Sending batch of " << session_->CountBufferedOperations();
    session_->FlushAsync(&flush_cb_);
  }
}

} // namespace twitter_demo
} // namespace kudu
