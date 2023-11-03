// Copyright (c) YugaByte, Inc.
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

#include "yb/util/monotime.h"

static constexpr const char* const kRedisKeyColumnName = "key";
static constexpr uint16_t kRedisClusterSlots = 16384;
static constexpr const char* const kExpireAt = "EXPIRE_AT";
static constexpr const char* const kExpireIn = "EXPIRE_IN";
static constexpr const char* const kWithScores = "WITHSCORES";
static constexpr const char* const kNX = "NX";
static constexpr const char* const kXX = "XX";
static constexpr const char* const kINCR = "INCR";
static constexpr const char* const kCH = "CH";
static constexpr int64_t kRedisMaxTtlMillis = std::numeric_limits<int64_t>::max() /
    yb::MonoTime::kNanosecondsPerMillisecond;
static constexpr int64_t kRedisMaxTtlSeconds = kRedisMaxTtlMillis /
    yb::MonoTime::kMillisecondsPerSecond;
static constexpr int64_t kRedisMinTtlMillis = std::numeric_limits<int64_t>::min() /
  yb::MonoTime::kNanosecondsPerMillisecond;
// SET with the EX flag does not support negative values. However, SETEX does.
static constexpr int64_t kRedisMinTtlSetExSeconds = 1;
