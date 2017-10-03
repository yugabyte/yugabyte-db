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
#ifndef KUDU_UTIL_STATUS_CALLBACK_H
#define KUDU_UTIL_STATUS_CALLBACK_H

#include "kudu/gutil/callback_forward.h"

namespace kudu {

class Status;

// A callback which takes a Status. This is typically used for functions which
// produce asynchronous results and may fail.
typedef Callback<void(const Status& status)> StatusCallback;

// To be used when a function signature requires a StatusCallback but none
// is needed.
extern void DoNothingStatusCB(const Status& status);

// A closure (callback without arguments) that returns a Status indicating
// whether it was successful or not.
typedef Callback<Status(void)> StatusClosure;

// To be used when setting a StatusClosure is optional.
extern Status DoNothingStatusClosure();

} // namespace kudu

#endif
