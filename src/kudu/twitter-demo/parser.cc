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

#include "kudu/twitter-demo/parser.h"

#include <time.h>

#include <glog/logging.h>
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

#include "kudu/gutil/stringprintf.h"
#include "kudu/util/jsonreader.h"

namespace kudu {
namespace twitter_demo {

TwitterEventParser::TwitterEventParser() {
}

TwitterEventParser::~TwitterEventParser() {
}

static Status ParseDelete(const JsonReader& r,
                          const rapidjson::Value* delete_obj,
                          TwitterEvent* event) {
  event->type = DELETE_TWEET;
  DeleteTweetEvent* e = &event->delete_event;

  const rapidjson::Value* status_obj;
  RETURN_NOT_OK(r.ExtractObject(delete_obj, "status", &status_obj));
  RETURN_NOT_OK(r.ExtractInt64(status_obj, "id", &e->tweet_id));
  RETURN_NOT_OK(r.ExtractInt64(status_obj, "user_id", &e->user_id));
  return Status::OK();
}

static Status ParseTweet(const JsonReader& r,
                         TwitterEvent* event) {
  event->type = TWEET;
  TweetEvent* e = &event->tweet_event;

  RETURN_NOT_OK(r.ExtractString(r.root(), "created_at", &e->created_at));
  RETURN_NOT_OK(r.ExtractInt64(r.root(), "id", &e->tweet_id));
  RETURN_NOT_OK(r.ExtractString(r.root(), "text", &e->text));
  RETURN_NOT_OK(r.ExtractString(r.root(), "source", &e->source));

  const rapidjson::Value* user_obj;
  RETURN_NOT_OK(r.ExtractObject(r.root(), "user", &user_obj));
  RETURN_NOT_OK(r.ExtractInt64(user_obj, "id", &e->user_id));
  RETURN_NOT_OK(r.ExtractString(user_obj, "name", &e->user_name));
  RETURN_NOT_OK(r.ExtractString(user_obj, "location", &e->user_location));
  RETURN_NOT_OK(r.ExtractString(user_obj, "description", &e->user_description));
  RETURN_NOT_OK(r.ExtractInt32(user_obj, "followers_count", &e->user_followers_count));
  RETURN_NOT_OK(r.ExtractInt32(user_obj, "friends_count", &e->user_friends_count));
  RETURN_NOT_OK(r.ExtractString(user_obj, "profile_image_url", &e->user_image_url));

  return Status::OK();
}

Status TwitterEventParser::Parse(const string& json, TwitterEvent* event) {
  JsonReader r(json);
  RETURN_NOT_OK(r.Init());
  const rapidjson::Value* delete_obj;
  Status s = r.ExtractObject(r.root(), "delete", &delete_obj);
  if (s.IsNotFound()) {
    return ParseTweet(r, event);
  }
  RETURN_NOT_OK(s);
  return ParseDelete(r, delete_obj, event);
}

string TwitterEventParser::ReformatTime(const string& twitter_time) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  // Example: Wed Aug 14 06:31:07 +0000 2013
  char* x = strptime(twitter_time.c_str(), "%a %b %d %H:%M:%S +0000 %Y", &t);
  if (*x != '\0') {
    return StringPrintf("unparseable date, date=%s, leftover=%s", twitter_time.c_str(), x);
  }

  char buf[100];
  size_t n = strftime(buf, arraysize(buf), "%Y%m%d%H%M%S", &t);
  CHECK_GT(n, 0);
  return string(buf);
}


} // namespace twitter_demo
} // namespace kudu
