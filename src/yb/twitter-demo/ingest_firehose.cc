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

#include <boost/thread/thread.hpp>
#include <curl/curl.h>
#include <fstream>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <stdint.h>

#include "yb/client/client.h"
#include "yb/gutil/macros.h"
#include "yb/gutil/once.h"
#include "yb/rpc/messenger.h"
#include "yb/master/master.h"
#include "yb/tserver/tserver_service.proxy.h"
#include "yb/twitter-demo/oauth.h"
#include "yb/twitter-demo/insert_consumer.h"
#include "yb/twitter-demo/twitter_streamer.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/slice.h"
#include "yb/util/status.h"

DEFINE_string(twitter_firehose_sink, "console",
              "Where to write firehose output.\n"
              "Valid values: console,rpc");
DEFINE_string(twitter_rpc_master_address, "localhost",
              "Address of master for the cluster to write to");

DEFINE_string(twitter_firehose_source, "api",
              "Where to obtain firehose input.\n"
              "Valid values: api,file");
DEFINE_string(twitter_firehose_file, "/dev/fd/0",
              "File to read firehose data from, if 'file' is configured.");


using std::string;

namespace yb {
namespace twitter_demo {

using client::sp::shared_ptr;
using tserver::TabletServerServiceProxy;

// Consumer which simply logs messages to the console.
class LoggingConsumer : public TwitterConsumer {
 public:
  virtual void ConsumeJSON(const Slice& json) OVERRIDE {
    std::cout << json.ToString();
  }
};

gscoped_ptr<TwitterConsumer> CreateInsertConsumer() {
  shared_ptr<client::KuduClient> client;
  CHECK_OK(client::KuduClientBuilder()
           .add_master_server_addr(FLAGS_twitter_rpc_master_address)
           .Build(&client));

  gscoped_ptr<InsertConsumer> ret(new InsertConsumer(client));
  CHECK_OK(ret->Init());
  return gscoped_ptr<TwitterConsumer>(ret.Pass()); // up-cast
}

static void IngestFromFile(const string& file, gscoped_ptr<TwitterConsumer> consumer) {
  std::ifstream in(file.c_str());
  CHECK(in.is_open()) << "Couldn't open " << file;

  string line;
  while (std::getline(in, line)) {
    consumer->ConsumeJSON(line);
  }
}

static int main(int argc, char** argv) {
  // Since this is meant to be run by a user, not a daemon,
  // log to stderr by default.
  FLAGS_logtostderr = 1;
  yb::ParseCommandLineFlags(&argc, &argv, true);
  yb::InitGoogleLoggingSafe(argv[0]);

  gscoped_ptr<TwitterConsumer> consumer;
  if (FLAGS_twitter_firehose_sink == "console") {
    consumer.reset(new LoggingConsumer);
  } else if (FLAGS_twitter_firehose_sink == "rpc") {
    consumer = CreateInsertConsumer();
  } else {
    LOG(FATAL) << "Unknown sink: " << FLAGS_twitter_firehose_sink;
  }

  if (FLAGS_twitter_firehose_source == "api") {
    TwitterStreamer streamer(consumer.get());
    CHECK_OK(streamer.Init());
    CHECK_OK(streamer.Start());
    CHECK_OK(streamer.Join());
  } else if (FLAGS_twitter_firehose_source == "file") {
    IngestFromFile(FLAGS_twitter_firehose_file, consumer.Pass());
  } else {
    LOG(FATAL) << "Unknown source: " << FLAGS_twitter_firehose_source;
  }
  return 0;
}

} // namespace twitter_demo
} // namespace yb

int main(int argc, char** argv) {
  return yb::twitter_demo::main(argc, argv);
}
