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

#include <iosfwd>
#include <string>

#include <gtest/gtest.h>

#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/util.h"

#include "yb/server/default-path-handlers.h"
#include "yb/server/webserver.h"

#include "yb/util/file_util.h"
#include "yb/util/curl_util.h"
#include "yb/util/env_util.h"
#include "yb/util/jsonreader.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/string_trim.h"
#include "yb/util/test_util.h"
#include "yb/util/zlib.h"

using std::string;
using std::vector;
using strings::Substitute;

DECLARE_int32(webserver_max_post_length_bytes);
DECLARE_uint64(webserver_compression_threshold_kb);
DECLARE_string(webserver_ca_certificate_file);
DECLARE_bool(webserver_strict_transport_security);
DECLARE_string(webserver_ssl_ciphers);
DECLARE_string(webserver_ssl_min_version);

namespace yb {

class WebserverTest : public YBTest {
 public:
  WebserverTest() {
    static_dir_ = GetTestPath("webserver-docroot");
    CHECK_OK(env_->CreateDir(static_dir_));
  }

  virtual WebserverOptions ServerOptions() {
    WebserverOptions opts;
    opts.port = 0;
    opts.doc_root = static_dir_;
    return opts;
  }

  void SetUp() override {
    YBTest::SetUp();

    server_.reset(new Webserver(ServerOptions(), "WebserverTest"));

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
  std::unique_ptr<Webserver> server_;
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

TEST_F(WebserverTest, TestHSTSOnHttp) {
  // Default behaviour should not included HSTS flag.
  curl_.set_return_headers(true);
  ASSERT_OK(curl_.FetchURL(url_, &buf_));

  // Check expected headers.
  ASSERT_STR_CONTAINS(buf_.ToString(), "X-Content-Type-Options: nosniff");
  ASSERT_STR_NOT_CONTAINS(buf_.ToString(), "Strict-Transport-Security: max-age=31536000");

  // Turning on HSTS flag shouldn't work on HTTP.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_webserver_strict_transport_security) = true;
  curl_.set_return_headers(true);

  ASSERT_OK(curl_.FetchURL(url_, &buf_));

  // Check expected headers.
  ASSERT_STR_CONTAINS(buf_.ToString(), "X-Content-Type-Options: nosniff");
  ASSERT_STR_NOT_CONTAINS(buf_.ToString(), "Strict-Transport-Security: max-age=31536000");
}

TEST_F(WebserverTest, TestHttpCompression) {
  std::ostringstream oss;
  string decoded_str;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_webserver_compression_threshold_kb) = 0;

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
  ASSERT_STR_CONTAINS(buf_.ToString(), "X-Content-Type-Options: nosniff");

  // Curl with compression disabled.
  curl_.set_return_headers(true);
  ASSERT_OK(curl_.FetchURL(url_, &buf_));

  // Check expected header.
  ASSERT_STR_CONTAINS(buf_.ToString(), "Content-Type:");
  ASSERT_STR_CONTAINS(buf_.ToString(), "X-Content-Type-Options: nosniff");

  // Check unexpected header.
  ASSERT_STR_NOT_CONTAINS(buf_.ToString(), "Content-Encoding: gzip");

  // Should have expected title.
  ASSERT_STR_CONTAINS(buf_.ToString(), "YugabyteDB");

  // Curl with compression enabled but not accepted by YugabyteDB.
  curl_.set_return_headers(true);
  ASSERT_OK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec,
                           {"Accept-Encoding: megaturbogzip, deflate, xz"}));
  // Check expected headers.
  ASSERT_STR_CONTAINS(buf_.ToString(), "HTTP/1.1 200 OK");
  ASSERT_STR_CONTAINS(buf_.ToString(), "X-Content-Type-Options: nosniff");

  // Check unexpected header.
  ASSERT_STR_NOT_CONTAINS(buf_.ToString(), "Content-Encoding: gzip");

  // Should have expected title.
  ASSERT_STR_CONTAINS(buf_.ToString(), "YugabyteDB");

}

TEST_F(WebserverTest, TestDefaultPaths) {
  // Test memz
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/memz?raw=1", ToString(addr_)),
                           &buf_));
#if YB_TCMALLOC_ENABLED
  ASSERT_STR_CONTAINS(buf_.ToString(), "Bytes in use by application");
#else
  ASSERT_STR_CONTAINS(buf_.ToString(), "not available unless tcmalloc is enabled");
#endif

  // Test varz -- check for one of the built-in gflags flags.
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/varz?raw=1", ToString(addr_)), &buf_));
  ASSERT_STR_CONTAINS(buf_.ToString(), "--v=");

  // Test varz json api
  ASSERT_OK(curl_.FetchURL(strings::Substitute("http://$0/api/v1/varz", ToString(addr_)), &buf_));
  // Output is a JSON array of 'flags'
  JsonReader jr(buf_.ToString());
  ASSERT_OK(jr.Init());
  vector<const rapidjson::Value *> entries;
  ASSERT_OK(jr.ExtractObjectArray(jr.root(), "flags", &entries));

  // Find flag with name 'v'
  auto it = std::find_if(entries.begin(), entries.end(), [&](const rapidjson::Value *value) {
    string name;
    if (!jr.ExtractString(value, "name", &name).ok()) {
      return false;
    }
    return name == "v";
  });
  ASSERT_NE(it, entries.end());

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
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_webserver_max_post_length_bytes) = 10;
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

class WebserverSecureTest : public WebserverTest {
 public:
  WebserverOptions ServerOptions() override {
    auto opts = WebserverTest::ServerOptions();
    opts.bind_interface = "127.0.0.2";

    const auto certs_dir = GetCertsDir();
    opts.certificate_file = JoinPathSegments(certs_dir, Format("node.$0.crt", opts.bind_interface));
    opts.private_key_file = JoinPathSegments(certs_dir, Format("node.$0.key", opts.bind_interface));
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_webserver_ca_certificate_file) =
        JoinPathSegments(certs_dir, "ca.crt");
    return opts;
  }

  void SetUp() override {
    WebserverTest::SetUp();

    url_ = Substitute("https://$0", ToString(addr_));
    curl_.set_ca_cert(FLAGS_webserver_ca_certificate_file);
  }
};

// Test HTTPS endpoint.
TEST_F(WebserverSecureTest, TestIndexPage) {
  ASSERT_OK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec, {} /* headers */));
}

// Test HSTS behaviour.
TEST_F(WebserverSecureTest, TestStrictTransportSecurity) {
  // Check that HSTS should not be present by default.
  curl_.set_return_headers(true);

  ASSERT_OK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec, {} /* headers */));
  ASSERT_STR_CONTAINS(buf_.ToString(), "X-Content-Type-Options: nosniff");
  ASSERT_STR_NOT_CONTAINS(buf_.ToString(), "Strict-Transport-Security: max-age=31536000");

  // Turn on HSTS GFlag and check headers.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_webserver_strict_transport_security) = true;
  curl_.set_return_headers(true);
  ASSERT_OK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec, {} /* headers */));
  ASSERT_STR_CONTAINS(buf_.ToString(), "X-Content-Type-Options: nosniff");
  ASSERT_STR_CONTAINS(buf_.ToString(), "Strict-Transport-Security: max-age=31536000");
}

class WebserverSSLConfigTest : public WebserverSecureTest {
 public:
  WebserverOptions ServerOptions() override {
    auto opts = WebserverSecureTest::ServerOptions();
    // Allow >= TLS 1.2
    opts.ssl_min_version = "tlsv1.2";
    // Allow ECDHE-RSA-AES256-GCM-SHA384 and AES256-SHA.
    opts.ssl_ciphers = "ECDHE-RSA-AES256-GCM-SHA384:AES256-SHA";

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_webserver_ssl_min_version) = opts.ssl_min_version;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_webserver_ssl_ciphers) = opts.ssl_ciphers;
    return opts;
  }
};

// Test that TLSv1.2 works, TLSv1.1 fails.
TEST_F(WebserverSSLConfigTest, TestTLSMinVersion) {
  curl_.set_ssl_version(CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2);
  ASSERT_OK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec, {}));

  curl_.set_ssl_version(CURL_SSLVERSION_TLSv1_1 | CURL_SSLVERSION_MAX_TLSv1_1);
  ASSERT_NOK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec, {}));
}

// Test that allowed cipher succeeds, disallowed cipher fails.
TEST_F(WebserverSSLConfigTest, TestSSLCiphers) {
  // Disallow curl client from using TLS v1.3 or above because the ssl_cipher_list variable is not
  // applicable for TLS v1.3 onwards.
  curl_.set_ssl_version(CURL_SSLVERSION_MAX_TLSv1_2);

  curl_.set_cipher_list("ECDHE-RSA-AES256-GCM-SHA384");
  ASSERT_OK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec, {}));

  curl_.set_cipher_list("AES128-SHA");
  ASSERT_NOK(curl_.FetchURL(url_, &buf_, EasyCurl::kDefaultTimeoutSec, {}));
}

} // namespace yb
