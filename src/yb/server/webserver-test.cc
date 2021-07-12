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
#include "yb/server/webserver.h"

#include <iosfwd>
#include <string>

#include <gflags/gflags.h>
#include <gtest/gtest.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/server/default-path-handlers.h"
#include "yb/util/curl_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/test_util.h"
#include "yb/util/zlib.h"

using std::string;
using strings::Substitute;

DECLARE_int32(webserver_max_post_length_bytes);
DECLARE_int64(webserver_compression_threshold_kb);

namespace yb {

class WebserverTest : public YBTest {
 public:
  WebserverTest() {
    static_dir_ = GetTestPath("webserver-docroot");
    CHECK_OK(env_->CreateDir(static_dir_));

    WebserverOptions opts;
    opts.port = 0;
    opts.doc_root = static_dir_;
    server_.reset(new Webserver(opts, "WebserverTest"));
  }

  void SetUp() override {
    YBTest::SetUp();

    AddDefaultPathHandlers(server_.get());
    ASSERT_OK(server_->Start());

    std::vector<Endpoint> addrs;
    ASSERT_OK(server_->GetBoundAddresses(&addrs));
    ASSERT_EQ(addrs.size(), 1);
    addr_ = addrs[0];
    url_ = Substitute("http://$0", ToString(addr_));
  }

 protected:
  EasyCurl curl_;
  faststring buf_;
  gscoped_ptr<Webserver> server_;
  Endpoint addr_;
  string url_;

  string static_dir_;
};

TEST_F(WebserverTest, TestIndexPage) {
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/", ToString(addr_)),
                           &buf_));
  // Should have expected title.
  ASSERT_STR_CONTAINS(buf_.ToString(), "Yugabyte");

  // Should have link to the root path handlers (Home).
  ASSERT_STR_CONTAINS(buf_.ToString(), "Home");
}

TEST_F(WebserverTest, TestHttpCompression) {
  std::ostringstream oss;
  string decoded_str;
  FLAGS_webserver_compression_threshold_kb = 0;

  // Curl with gzip compression enabled.
  ASSERT_OK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec,
                           {"Accept-Encoding: deflate, br, gzip"}));

  // If compressed successfully, we should be able to uncompress.
  ASSERT_OK(zlib::Uncompress(Slice(buf_.ToString()), &oss));
  decoded_str = oss.str();

  // Should have expected title.
  ASSERT_STR_CONTAINS(decoded_str, "YugabyteDB");

  // Should have expected header when compressed with headers returned.
  curl_.set_return_headers(true);
  ASSERT_OK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec,
                          {"Accept-Encoding: deflate, megaturbogzip,  gzip , br"}));
  ASSERT_STR_CONTAINS(buf_.ToString(), "Content-Encoding: gzip");


  // Curl with compression disabled.
  curl_.set_return_headers(true);
  ASSERT_OK(curl_.FetchURL(url_, &buf_));
  // Check expected header.
  ASSERT_STR_CONTAINS(buf_.ToString(), "Content-Type:");

  // Check unexpected header.
  ASSERT_STR_NOT_CONTAINS(buf_.ToString(), "Content-Encoding: gzip");

  // Should have expected title.
  ASSERT_STR_CONTAINS(buf_.ToString(), "YugabyteDB");

  // Curl with compression enabled but not accepted by YugabyteDB.
  curl_.set_return_headers(true);
  ASSERT_OK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec,
                           {"Accept-Encoding: megaturbogzip, deflate, xz"}));
  // Check expected header.
  ASSERT_STR_CONTAINS(buf_.ToString(), "HTTP/1.1 200 OK");

  // Check unexpected header.
  ASSERT_STR_NOT_CONTAINS(buf_.ToString(), "Content-Encoding: gzip");

  // Should have expected title.
  ASSERT_STR_CONTAINS(buf_.ToString(), "YugabyteDB");

}

TEST_F(WebserverTest, TestDefaultPaths) {
  // Test memz
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/memz?raw=1", ToString(addr_)),
                           &buf_));
#ifdef TCMALLOC_ENABLED
  ASSERT_STR_CONTAINS(buf_.ToString(), "Bytes in use by application");
#else
  ASSERT_STR_CONTAINS(buf_.ToString(), "not available unless tcmalloc is enabled");
#endif

  // Test varz -- check for one of the built-in gflags flags.
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/varz?raw=1", ToString(addr_)),
                           &buf_));
  ASSERT_STR_CONTAINS(buf_.ToString(), "--v=");

  // Test status.
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/status", ToString(addr_)),
                           &buf_));
  ASSERT_STR_EQ_VERBOSE_TRIMMED("{}", buf_.ToString());
}

// Used in symbolization test below.
void SomeMethodForSymbolTest1() {}
// Used in symbolization test below.
void SomeMethodForSymbolTest2() {}

TEST_F(WebserverTest, TestPprofPaths) {
  // Test /pprof/cmdline GET
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/pprof/cmdline", ToString(addr_)),
                           &buf_));
  ASSERT_STR_CONTAINS(buf_.ToString(), "webserver-test");
  ASSERT_TRUE(!HasSuffixString(buf_.ToString(), string("\x00", 1)))
    << "should not have trailing NULL: " << Slice(buf_).ToDebugString();

  // Test /pprof/symbol GET
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/pprof/symbol", ToString(addr_)),
                           &buf_));
  ASSERT_EQ(buf_.ToString(), "num_symbols: 1");

  // Test /pprof/symbol POST
  {
    // Formulate a request with some valid symbol addresses.
    string req = StringPrintf("%p+%p",
                              &SomeMethodForSymbolTest1,
                              &SomeMethodForSymbolTest2);
    SCOPED_TRACE(req);
    ASSERT_OK(curl_.PostToURL(strings::Substitute("http://$0/pprof/symbol", ToString(addr_)),
                              req, &buf_));
    ASSERT_EQ(buf_.ToString(),
              StringPrintf("%p\tyb::SomeMethodForSymbolTest1()\n"
                           "%p\tyb::SomeMethodForSymbolTest2()\n",
                           &SomeMethodForSymbolTest1,
                           &SomeMethodForSymbolTest2));
  }
}

// Send a POST request with too much data. It should reject
// the request with the correct HTTP error code.
TEST_F(WebserverTest, TestPostTooBig) {
  FLAGS_webserver_max_post_length_bytes = 10;
  string req(10000, 'c');
  Status s = curl_.PostToURL(strings::Substitute("http://$0/pprof/symbol", ToString(addr_)),
                             req, &buf_);
  ASSERT_EQ("Remote error: HTTP 413", s.ToString(/* no file/line */ false));
}

// Test that static files are served and that directory listings are
// disabled.
TEST_F(WebserverTest, TestStaticFiles) {
  // Fetch a non-existent static file.
  Status s = curl_.FetchURL(strings::Substitute("http://$0/foo.txt", ToString(addr_)),
                            &buf_);
  ASSERT_EQ("Remote error: HTTP 404", s.ToString(/* no file/line */ false));

  // Create the file and fetch again. This time it should succeed.
  ASSERT_OK(WriteStringToFile(env_.get(), "hello world",
                              strings::Substitute("$0/foo.txt", static_dir_)));
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/foo.txt", ToString(addr_)),
                           &buf_));
  ASSERT_EQ("hello world", buf_.ToString());

  // Create a directory and ensure that subdirectory listing is disabled.
  ASSERT_OK(env_->CreateDir(strings::Substitute("$0/dir", static_dir_)));
  s = curl_.FetchURL(strings::Substitute("http://$0/dir/", ToString(addr_)),
                     &buf_);
  ASSERT_EQ("Remote error: HTTP 403", s.ToString(/* no file/line */ false));
}

} // namespace yb
