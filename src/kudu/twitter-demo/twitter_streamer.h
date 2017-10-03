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
#ifndef KUDU_TWITTER_DEMO_TWITTER_STREAMER_H
#define KUDU_TWITTER_DEMO_TWITTER_STREAMER_H

#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

#include "kudu/util/faststring.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

namespace kudu {
namespace twitter_demo {

class TwitterConsumer {
 public:
  virtual void ConsumeJSON(const Slice& json) = 0;
  virtual ~TwitterConsumer() {}
};

class TwitterStreamer {
 public:
  explicit TwitterStreamer(TwitterConsumer* consumer)
    : consumer_(consumer) {
  }

  Status Init();
  Status Start();
  Status Join();

 private:
  friend size_t DataReceivedCallback(void* buffer, size_t size, size_t nmemb, void* user_ptr);
  void StreamThread();
  Status DoStreaming();
  size_t DataReceived(const Slice& data);

  boost::thread thread_;
  boost::mutex lock_;
  Status stream_status_;

  faststring recv_buf_;

  TwitterConsumer* consumer_;

  DISALLOW_COPY_AND_ASSIGN(TwitterStreamer);
};


} // namespace twitter_demo
} // namespace kudu
#endif
