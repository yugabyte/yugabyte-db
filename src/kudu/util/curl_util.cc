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

#include "kudu/util/curl_util.h"

#include "kudu/gutil/strings/substitute.h"

#include <curl/curl.h>
#include <glog/logging.h>

namespace kudu {

namespace {

inline Status TranslateError(CURLcode code) {
  if (code == CURLE_OK) {
    return Status::OK();
  }
  return Status::NetworkError("curl error", curl_easy_strerror(code));
}

extern "C" {
size_t WriteCallback(void* buffer, size_t size, size_t nmemb, void* user_ptr) {
  size_t real_size = size * nmemb;
  faststring* buf = reinterpret_cast<faststring*>(user_ptr);
  CHECK_NOTNULL(buf)->append(reinterpret_cast<const uint8_t*>(buffer), real_size);
  return real_size;
}
} // extern "C"

} // anonymous namespace

EasyCurl::EasyCurl() {
  curl_ = curl_easy_init();
  CHECK(curl_) << "Could not init curl";
}

EasyCurl::~EasyCurl() {
  curl_easy_cleanup(curl_);
}

Status EasyCurl::FetchURL(const std::string& url, faststring* buf) {
  return DoRequest(url, nullptr, buf);
}

Status EasyCurl::PostToURL(const std::string& url,
                           const std::string& post_data,
                           faststring* dst) {
  return DoRequest(url, &post_data, dst);
}

Status EasyCurl::DoRequest(const std::string& url,
                           const std::string* post_data,
                           faststring* dst) {
  CHECK_NOTNULL(dst)->clear();

  RETURN_NOT_OK(TranslateError(curl_easy_setopt(curl_, CURLOPT_URL, url.c_str())));
  RETURN_NOT_OK(TranslateError(curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteCallback)));
  RETURN_NOT_OK(TranslateError(curl_easy_setopt(curl_, CURLOPT_WRITEDATA,
                                                static_cast<void *>(dst))));
  if (post_data) {
    RETURN_NOT_OK(TranslateError(curl_easy_setopt(curl_, CURLOPT_POSTFIELDS,
                                                  post_data->c_str())));
  }

  RETURN_NOT_OK(TranslateError(curl_easy_perform(curl_)));
  long rc; // NOLINT(runtime/int) curl wants a long
  RETURN_NOT_OK(TranslateError(curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &rc)));
  if (rc != 200) {
    return Status::RemoteError(strings::Substitute("HTTP $0", rc));
  }

  return Status::OK();
}

} // namespace kudu
