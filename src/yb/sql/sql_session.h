//--------------------------------------------------------------------------------------------------
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
//
// This class represents a SQL session of a client connection (e.g. CQL client connection).
//--------------------------------------------------------------------------------------------------
#ifndef YB_SQL_SQL_SESSION_H_
#define YB_SQL_SQL_SESSION_H_

#include <memory>
#include <mutex>
#include <string>

#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "yb/master/master_defaults.h"

namespace yb {
namespace sql {

static const char* const kUndefinedKeyspace = ""; // Must be empty string.

class SqlSession {
 public:
  // Public types.
  typedef std::shared_ptr<SqlSession> SharedPtr;
  typedef std::shared_ptr<const SqlSession> SharedPtrConst;

  // Constructors.
  SqlSession() : current_keyspace_(kUndefinedKeyspace) { }
  virtual ~SqlSession() { }

  // Access functions for current keyspace. It can be accessed by mutiple calls in parallel so
  // they need to be thread-safe for shared reads / exclusive writes.
  std::string current_keyspace() const {
    boost::shared_lock<boost::shared_mutex> l(current_keyspace_mutex_);
    return current_keyspace_;
  }
  void set_current_keyspace(const std::string& keyspace) {
    boost::lock_guard<boost::shared_mutex> l(current_keyspace_mutex_);
    current_keyspace_ = keyspace;
  }

 private:
  // Mutex to protect access to current_keyspace_.
  mutable boost::shared_mutex current_keyspace_mutex_;

  // Current keyspace.
  std::string current_keyspace_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_SQL_SESSION_H_
