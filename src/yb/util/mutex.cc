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
// Portions (c) 2011 The Chromium Authors.

#include "yb/util/mutex.h"

#include "yb/util/logging.h"

#include "yb/util/stack_trace.h"
#include "yb/util/env.h"

namespace yb {

Mutex::Mutex()
#ifndef NDEBUG
  : owning_tid_(0),
    stack_trace_(new StackTrace())
#endif
{
#ifndef NDEBUG
  // In debug, setup attributes for lock error checking.
  pthread_mutexattr_t mta;
  int rv = pthread_mutexattr_init(&mta);
  DCHECK_EQ(0, rv) << ". " << strerror(rv);
  rv = pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_ERRORCHECK);
  DCHECK_EQ(0, rv) << ". " << strerror(rv);
  rv = pthread_mutex_init(&native_handle_, &mta);
  DCHECK_EQ(0, rv) << ". " << strerror(rv);
  rv = pthread_mutexattr_destroy(&mta);
  DCHECK_EQ(0, rv) << ". " << strerror(rv);
#else
  // In release, go with the default lock attributes.
  pthread_mutex_init(&native_handle_, NULL);
#endif
}

Mutex::~Mutex() {
  int rv = pthread_mutex_destroy(&native_handle_);
  DCHECK_EQ(0, rv) << ". " << strerror(rv);
}

bool Mutex::TryAcquire() {
  int rv = pthread_mutex_trylock(&native_handle_);
#ifndef NDEBUG
  DCHECK(rv == 0 || rv == EBUSY) << ". " << strerror(rv)
      << ". Owner tid: " << owning_tid_ << "; Self tid: " << Env::Default()->gettid()
      << "; Owner stack: " << std::endl << stack_trace_->Symbolize();;
  if (rv == 0) {
    CheckUnheldAndMark();
  }
#endif
  return rv == 0;
}

void Mutex::Acquire() {
  int rv = pthread_mutex_lock(&native_handle_);
#ifndef NDEBUG
  DCHECK_EQ(0, rv) << ". " << strerror(rv)
      << ". Owner tid: " << owning_tid_ << "; Self tid: " << Env::Default()->gettid()
      << "; Owner stack: " << std::endl << stack_trace_->Symbolize();;
  CheckUnheldAndMark();
#else
  DCHECK(!rv);
#endif
}

void Mutex::Release() {
#ifndef NDEBUG
  CheckHeldAndUnmark();
#endif
  int rv = pthread_mutex_unlock(&native_handle_);
  DCHECK_EQ(0, rv) << ". " << strerror(rv);
}

#ifndef NDEBUG
void Mutex::AssertAcquired() const {
  DCHECK_EQ(Env::Default()->gettid(), owning_tid_);
}

void Mutex::CheckHeldAndUnmark() {
  AssertAcquired();
  owning_tid_ = 0;
  stack_trace_->Reset();
}

void Mutex::CheckUnheldAndMark() {
  DCHECK_EQ(0, owning_tid_);
  owning_tid_ = Env::Default()->gettid();
  stack_trace_->Collect();
}

#endif

} // namespace yb
