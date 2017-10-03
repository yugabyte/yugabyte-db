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

#ifndef KUDU_UTIL_OID_GENERATOR_H
#define KUDU_UTIL_OID_GENERATOR_H

#include <boost/uuid/uuid_generators.hpp>
#include <string>

#include "kudu/gutil/macros.h"
#include "kudu/util/locks.h"

namespace kudu {

// Generates a unique 32byte id, based on uuid v4.
// This class is thread safe
class ObjectIdGenerator {
 public:
  ObjectIdGenerator() {}
  ~ObjectIdGenerator() {}

  std::string Next();

 private:
  DISALLOW_COPY_AND_ASSIGN(ObjectIdGenerator);

  typedef simple_spinlock LockType;

  LockType oid_lock_;
  boost::uuids::random_generator oid_generator_;
};

} // namespace kudu

#endif
