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
#ifndef KUDU_BOOST_MUTEX_UTILS_H
#define KUDU_BOOST_MUTEX_UTILS_H


// Similar to boost::lock_guard except that it takes
// a lock pointer, and checks against NULL. If the
// pointer is NULL, does nothing. Otherwise guards
// with the lock.
template<class LockType>
class lock_guard_maybe {
 public:
  explicit lock_guard_maybe(LockType *l) :
    lock_(l) {
    if (l != NULL) {
      l->lock();
    }
  }

  ~lock_guard_maybe() {
    if (lock_ != NULL) {
      lock_->unlock();
    }
  }

 private:
  LockType *lock_;
};

#endif
