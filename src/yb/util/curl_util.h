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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <curl/curl.h>

#include <string>
#include <vector>

#include <boost/optional.hpp>

#include "yb/gutil/macros.h"

#include "yb/util/status_fwd.h"

typedef void CURL;

namespace yb {

class faststring;

class CurlGlobalInitializer {
 public:
  CurlGlobalInitializer();
  ~CurlGlobalInitializer();
};

// Simple wrapper around curl's "easy" interface, allowing the user to
// fetch web pages into memory using a blocking API.
//
// This is not thread-safe.
class EasyCurl {
 public:
  EasyCurl();
  ~EasyCurl();

  // Fetch the given URL into the provided buffer.
  // Any existing data in the buffer is replaced.
  // The optional param 'headers' holds additional headers.
  // e.g. {"Accept-Encoding: gzip"}
  Status FetchURL(
      const std::string& url,
      faststring* dst,
      int64_t timeout_sec = kDefaultTimeoutSec,
      const std::vector<std::string>& headers = {});

  // Issue an HTTP POST to the given URL with the given data.
  // Returns results in 'dst' as above.
  Status PostToURL(
      const std::string& url,
      const std::string& post_data,
      faststring* dst,
      int64_t timeout_sec = kDefaultTimeoutSec);

  Status PostToURL(
      const std::string& url,
      const std::string& post_data,
      const std::string& content_type,
      faststring* dst,
      int64_t timeout_sec = kDefaultTimeoutSec);

  std::string EscapeString(const std::string& data);

  static const int64_t kDefaultTimeoutSec = 600;

  void set_return_headers(bool v) {
    return_headers_ = v;
  }

  void set_follow_redirects(bool v) {
    follow_redirects_ = v;
  }

  void set_ca_cert(const std::string& v) {
    ca_cert_ = v;
  }

  void set_ssl_version(int64 ssl_version) {
    ssl_version_ = ssl_version;
  }

  void set_cipher_list(const std::string& cipher_list) {
    cipher_list_ = cipher_list;
  }

 private:
  // Do a request. If 'post_data' is non-NULL, does a POST.
  // Otherwise, does a GET.
  Status DoRequest(
      const std::string& url,
      const boost::optional<const std::string>& post_data,
      const boost::optional<const std::string>& content_type,
      int64_t timeout_sec,
      faststring* dst,
      const std::vector<std::string>& headers = {});

  CURL* curl_;
  // Whether to return the HTTP headers with the response.
  bool return_headers_ = false;
  // Whether to follow HTTP redirects.
  bool follow_redirects_ = false;
  // Path to CA certificates. Defaults to system-wide registered CAs if not set.
  std::string ca_cert_;
  // The SSL version to use. Defaults to let CURL choose the appropriate SSL version.
  int64 ssl_version_ = 0;
  // The allowed SSL ciphers to use. Defaults to system default.
  std::string cipher_list_;
  DISALLOW_COPY_AND_ASSIGN(EasyCurl);
};

} // namespace yb
