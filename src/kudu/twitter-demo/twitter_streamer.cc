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

#include "kudu/twitter-demo/twitter_streamer.h"

#include <boost/thread/locks.hpp>
#include <boost/thread/thread.hpp>
#include <curl/curl.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <string>
#include <string.h>

#include "kudu/twitter-demo/oauth.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/once.h"
#include "kudu/util/slice.h"
#include "kudu/util/status.h"

using std::string;

const char* kTwitterUrl = "https://stream.twitter.com/1.1/statuses/sample.json";

// Defaults are for the "kudu-demo" app under the "KuduProject" account.
// See https://dev.twitter.com/apps/4906821/oauth if you have credentials.
DEFINE_string(twitter_consumer_key, "lRXXfnhNGhFO1DdAorVJeQ",
              "Twitter API consumer key");
DEFINE_string(twitter_consumer_secret, "5Enn1Uwy3mHdhwSVrJEbd24whGiHsA2YGJ0O28E",
              "Twitter API consumer secret");
DEFINE_string(twitter_token_key, "1653869436-7QncqwFkMaOS6rWNeHpwNQZ8li1CFbJp0QNOEpE",
              "Twitter API access token key");
DEFINE_string(twitter_token_secret, "1t3UPOJc6nkThvBPcCPGAj3gHB3mB97F3zraoRkKMA",
              "Twitter API access token secret");

namespace kudu {
namespace twitter_demo {

////////////////////////////////////////////////////////////
// Curl utilities
////////////////////////////////////////////////////////////

static void DoInitCurl() {
  CHECK_EQ(0, curl_global_init(CURL_GLOBAL_ALL));
}

static void InitCurl() {
  static GoogleOnceType once = GOOGLE_ONCE_INIT;
  GoogleOnceInit(&once, &DoInitCurl);
}

// Scope-based deleters for various libcurl types.
template<class CurlType>
class CurlDeleter {
 public:
  explicit CurlDeleter(CurlType* curl) : curl_(curl) {}
  ~CurlDeleter() {
    if (curl_) DoFree();
  }
 private:
  // Will be specialized for each type.
  void DoFree();
  CurlType* curl_;

  DISALLOW_COPY_AND_ASSIGN(CurlDeleter);
};

template<>
void CurlDeleter<CURL>::DoFree() {
  curl_easy_cleanup(curl_);
}

template<>
void CurlDeleter<curl_slist>::DoFree() {
  curl_slist_free_all(curl_);
}

////////////////////////////////////////////////////////////
// TwitterStreamer implementation
////////////////////////////////////////////////////////////

Status TwitterStreamer::Init() {
  if (FLAGS_twitter_consumer_key.empty()) {
    return Status::InvalidArgument("Missing flag", "--twitter_consumer_key");
  }
  if (FLAGS_twitter_consumer_secret.empty()) {
    return Status::InvalidArgument("Missing flag", "--twitter_consumer_secret");
  }
  if (FLAGS_twitter_token_key.empty()) {
    return Status::InvalidArgument("Missing flag", "--twitter_token_key");
  }
  if (FLAGS_twitter_token_secret.empty()) {
    return Status::InvalidArgument("Missing flag", "--twitter_token_secret");
  }
  return Status::OK();
}

Status TwitterStreamer::Start() {
  CHECK(!thread_.joinable());

  thread_ = boost::thread(&TwitterStreamer::StreamThread, this);
  return Status::OK();
}

Status TwitterStreamer::Join() {
  thread_.join();
  return stream_status_;
}

// C-style curl callback for data
size_t DataReceivedCallback(void* buffer, size_t size, size_t nmemb, void* user_ptr) {
  TwitterStreamer* streamer = DCHECK_NOTNULL(reinterpret_cast<TwitterStreamer*>(user_ptr));
  size_t total_size = size * nmemb;
  Slice data(reinterpret_cast<const uint8_t*>(buffer), total_size);
  return streamer->DataReceived(data);
}

void TwitterStreamer::StreamThread() {
  Status s = DoStreaming();
  if (!s.ok()) {
    LOG(ERROR) << "Streaming thread failed: " << s.ToString();
    boost::lock_guard<boost::mutex> l(lock_);
    stream_status_ = s;
  }
}

Status TwitterStreamer::DoStreaming() {
  OAuthRequest req("GET", kTwitterUrl);
  req.AddStandardOAuthFields(FLAGS_twitter_consumer_key, FLAGS_twitter_token_key);
  string auth_header = req.AuthHeader(FLAGS_twitter_consumer_secret, FLAGS_twitter_token_secret);
  VLOG(1) << auth_header;

  InitCurl();
  CURL* curl = curl_easy_init();
  CurlDeleter<CURL> delete_curl(curl);
  if (!curl) {
    return Status::NetworkError("curl_easy_init failed");
  }
  CHECK(curl);

  // Disable SSL verification so we don't have to set up a
  // trust store database of any kind.
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_URL, kTwitterUrl);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, DataReceivedCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);

  struct curl_slist* headers = NULL;
  CurlDeleter<curl_slist> delete_headers(headers);
  headers = curl_slist_append(headers, auth_header.c_str());

  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    return Status::NetworkError("curl_easy_perfom failed", curl_easy_strerror(res));
  }

  return Status::OK();
}

size_t TwitterStreamer::DataReceived(const Slice& slice) {
  recv_buf_.append(slice.data(), slice.size());

  // Chop the received data into lines.
  while (true) {
    void* newline_ptr = memchr(recv_buf_.data(), '\n', recv_buf_.size());
    if (newline_ptr == NULL) {
      // no newlines
      break;
    }
    int newline_idx = reinterpret_cast<uintptr_t>(newline_ptr) -
      reinterpret_cast<uintptr_t>(recv_buf_.data());

    Slice line(recv_buf_.data(), newline_idx);
    consumer_->ConsumeJSON(line);

    // Copy remaining data back to front of the buffer
    int rem_size = recv_buf_.size() - newline_idx - 1;
    memmove(recv_buf_.data(), &recv_buf_[newline_idx + 1], rem_size);
    // Resize to only have the front
    recv_buf_.resize(rem_size);
  }

  return slice.size();
}

} // namespace twitter_demo
} // namespace kudu
