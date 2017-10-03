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

#include <gflags/gflags.h>
#include <gtest/gtest.h>
#include <string>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/server/default-path-handlers.h"
#include "kudu/server/webserver.h"
#include "kudu/util/curl_util.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/test_util.h"

using std::string;

DECLARE_int32(webserver_max_post_length_bytes);

namespace kudu {

class WebserverTest : public KuduTest {
 public:
  WebserverTest() {
    static_dir_ = GetTestPath("webserver-docroot");
    CHECK_OK(env_->CreateDir(static_dir_));

    WebserverOptions opts;
    opts.port = 0;
    opts.doc_root = static_dir_;
    server_.reset(new Webserver(opts));
  }

  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();

    AddDefaultPathHandlers(server_.get());
    ASSERT_OK(server_->Start());

    vector<Sockaddr> addrs;
    ASSERT_OK(server_->GetBoundAddresses(&addrs));
    ASSERT_EQ(addrs.size(), 1);
    addr_ = addrs[0];
  }

 protected:
  EasyCurl curl_;
  faststring buf_;
  gscoped_ptr<Webserver> server_;
  Sockaddr addr_;

  string static_dir_;
};

TEST_F(WebserverTest, TestIndexPage) {
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/", addr_.ToString()),
                           &buf_));
  // Should have expected title.
  ASSERT_STR_CONTAINS(buf_.ToString(), "Kudu");

  // Should have link to default path handlers (e.g memz)
  ASSERT_STR_CONTAINS(buf_.ToString(), "memz");
}

TEST_F(WebserverTest, TestDefaultPaths) {
  // Test memz
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/memz?raw=1", addr_.ToString()),
                           &buf_));
#ifdef TCMALLOC_ENABLED
  ASSERT_STR_CONTAINS(buf_.ToString(), "Bytes in use by application");
#else
  ASSERT_STR_CONTAINS(buf_.ToString(), "not available unless tcmalloc is enabled");
#endif

  // Test varz -- check for one of the built-in gflags flags.
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/varz?raw=1", addr_.ToString()),
                           &buf_));
  ASSERT_STR_CONTAINS(buf_.ToString(), "--v=");
}

// Used in symbolization test below.
void SomeMethodForSymbolTest1() {}
// Used in symbolization test below.
void SomeMethodForSymbolTest2() {}

TEST_F(WebserverTest, TestPprofPaths) {
  // Test /pprof/cmdline GET
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/pprof/cmdline", addr_.ToString()),
                           &buf_));
  ASSERT_STR_CONTAINS(buf_.ToString(), "webserver-test");
  ASSERT_TRUE(!HasSuffixString(buf_.ToString(), string("\x00", 1)))
    << "should not have trailing NULL: " << Slice(buf_).ToDebugString();

  // Test /pprof/symbol GET
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/pprof/symbol", addr_.ToString()),
                           &buf_));
  ASSERT_EQ(buf_.ToString(), "num_symbols: 1");

  // Test /pprof/symbol POST
  {
    // Formulate a request with some valid symbol addresses.
    string req = StringPrintf("%p+%p",
                              &SomeMethodForSymbolTest1,
                              &SomeMethodForSymbolTest2);
    SCOPED_TRACE(req);
    ASSERT_OK(curl_.PostToURL(strings::Substitute("http://$0/pprof/symbol", addr_.ToString()),
                              req, &buf_));
    ASSERT_EQ(buf_.ToString(),
              StringPrintf("%p\tkudu::SomeMethodForSymbolTest1()\n"
                           "%p\tkudu::SomeMethodForSymbolTest2()\n",
                           &SomeMethodForSymbolTest1,
                           &SomeMethodForSymbolTest2));
  }
}

// Send a POST request with too much data. It should reject
// the request with the correct HTTP error code.
TEST_F(WebserverTest, TestPostTooBig) {
  FLAGS_webserver_max_post_length_bytes = 10;
  string req(10000, 'c');
  Status s = curl_.PostToURL(strings::Substitute("http://$0/pprof/symbol", addr_.ToString()),
                             req, &buf_);
  ASSERT_EQ("Remote error: HTTP 413", s.ToString());
}

// Test that static files are served and that directory listings are
// disabled.
TEST_F(WebserverTest, TestStaticFiles) {
  // Fetch a non-existent static file.
  Status s = curl_.FetchURL(strings::Substitute("http://$0/foo.txt", addr_.ToString()),
                            &buf_);
  ASSERT_EQ("Remote error: HTTP 404", s.ToString());

  // Create the file and fetch again. This time it should succeed.
  ASSERT_OK(WriteStringToFile(env_.get(), "hello world",
                              strings::Substitute("$0/foo.txt", static_dir_)));
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/foo.txt", addr_.ToString()),
                           &buf_));
  ASSERT_EQ("hello world", buf_.ToString());

  // Create a directory and ensure that subdirectory listing is disabled.
  ASSERT_OK(env_->CreateDir(strings::Substitute("$0/dir", static_dir_)));
  s = curl_.FetchURL(strings::Substitute("http://$0/dir/", addr_.ToString()),
                     &buf_);
  ASSERT_EQ("Remote error: HTTP 403", s.ToString());
}

} // namespace kudu
