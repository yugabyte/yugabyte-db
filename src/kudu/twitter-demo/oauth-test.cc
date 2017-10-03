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


#include "kudu/twitter-demo/oauth.h"

#include <gtest/gtest.h>
#include <string>

#include "kudu/util/debug/leakcheck_disabler.h"

using std::string;

namespace kudu {
namespace twitter_demo {

// Test case from Appendix A of the OAuth 1.0 standard:
// http://oauth.net/core/1.0/
TEST(OAuthTest, TestSignature) {
  const string kConsumerKey = "dpf43f3p2l4k3l03";
  const string kConsumerSecret = "kd94hf93k423kf44";
  const string kTokenKey = "nnch734d00sl2jdk";
  const string kTokenSecret = "pfkkdhi9sl3r4s00";

  // Necessary to squelch a leak originating in the NSS SSL library.
  debug::ScopedLeakCheckDisabler disabler;

  OAuthRequest req("GET", "http://photos.example.net/photos");

  req.AddPair("oauth_consumer_key", kConsumerKey);
  req.AddPair("oauth_token", kTokenKey);
  req.AddPair("oauth_signature_method", "HMAC-SHA1");
  req.AddPair("oauth_timestamp", "1191242096");
  req.AddPair("oauth_nonce", "kllo9940pd9333jh");
  req.AddPair("oauth_version", "1.0");
  req.AddPair("file", "vacation.jpg");
  req.AddPair("size", "original");
  string base = req.SignatureBaseString();
  ASSERT_EQ(string("GET&http%3A%2F%2Fphotos.example.net%2Fphotos&file%3Dvacation.jpg%26"
                   "oauth_consumer_key%3Ddpf43f3p2l4k3l03%26oauth_nonce%3Dkllo9940pd9333jh%26"
                   "oauth_signature_method%3DHMAC-SHA1%26oauth_timestamp%3D1191242096%26"
                   "oauth_token%3Dnnch734d00sl2jdk%26oauth_version%3D1.0%26size%3Doriginal"),
            base);

  string sig = req.Signature(kConsumerSecret, kTokenSecret);
  ASSERT_EQ("tR3+Ty81lMeYAr/Fid0kMTYa/WM=", sig);
}


} // namespace twitter_demo
} // namespace kudu
