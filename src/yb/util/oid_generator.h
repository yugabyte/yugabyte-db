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

#ifndef YB_UTIL_OID_GENERATOR_H
#define YB_UTIL_OID_GENERATOR_H

#include <string>

#include <boost/uuid/uuid_generators.hpp>

#include "yb/gutil/macros.h"
#include "yb/util/locks.h"

namespace yb {

// Generates a unique 32byte id, based on uuid v4.
// This class is thread safe
class ObjectIdGenerator {
 public:
  ObjectIdGenerator() {}
  ~ObjectIdGenerator() {}

  std::string Next(bool binary_id = false);

 private:
  typedef simple_spinlock LockType;

  // Multiple instances of OID generators with corresponding locks are used to
  // avoid bottlenecking on a single lock.
  static const int kNumOidGenerators = 17;
  LockType oid_lock_[kNumOidGenerators];
  boost::uuids::random_generator oid_generator_[kNumOidGenerators];

  DISALLOW_COPY_AND_ASSIGN(ObjectIdGenerator);
};

} // namespace yb

#endif // YB_UTIL_OID_GENERATOR_H
