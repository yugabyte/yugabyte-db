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

#include "yb/util/oid_generator.h"

#include <mutex>
#include <random>
#include <string>

#include <boost/uuid/uuid_generators.hpp>

#include "yb/gutil/strings/escaping.h"
#include "yb/util/cast.h"
#include "yb/util/locks.h"
#include "yb/util/thread.h"

using std::string;

namespace yb {

namespace {

class Generator {
 public:
  std::string Next(bool binary_id) {
    // Use the thread id to select a random oid generator.
    auto& entry = entries_[yb::Thread::UniqueThreadId() % kNumOidGenerators];
    boost::uuids::uuid oid;
    {
      std::lock_guard<LockType> lock(entry.lock);
      oid = entry.generator();
    }

    return binary_id ? string(to_char_ptr(oid.data), sizeof(oid.data))
                     : b2a_hex(to_char_ptr(oid.data), sizeof(oid.data));
  }

 private:
  typedef simple_spinlock LockType;

  // Multiple instances of OID generators with corresponding locks are used to
  // avoid bottlenecking on a single lock.
  static const int kNumOidGenerators = 17;
  struct Entry {
    LockType lock;
    boost::uuids::random_generator generator;
  };
  Entry entries_[kNumOidGenerators];
};

} // namespace

// Generates a unique 32byte id, based on uuid v4.
// This class is thread safe

std::string GenerateObjectId(bool binary_id) {
  static Generator generator;
  return generator.Next(binary_id);
}

namespace platform
{
int AtFork(void (*prepare)(), void (*parent)(), void (*child)()) noexcept
{
  return ::pthread_atfork(prepare, parent, child);
}
}

class TlsRandomNumberGenerator
{
public:
  TlsRandomNumberGenerator() noexcept
  {
    Seed();
    platform::AtFork(nullptr, nullptr, OnFork);
  }

  static FastRandomNumberGenerator &engine() noexcept { return engine_; }

private:
  static thread_local FastRandomNumberGenerator engine_;

  static void OnFork() noexcept { Seed(); }

  static void Seed() noexcept
  {
    std::random_device random_device;
    std::seed_seq seed_seq{random_device(), random_device(), random_device(), random_device()};
    engine_.seed(seed_seq);
  }
};

thread_local FastRandomNumberGenerator TlsRandomNumberGenerator::engine_{};

FastRandomNumberGenerator &OtelRandom::GetRandomNumberGenerator() noexcept
{
  static thread_local TlsRandomNumberGenerator random_number_generator{};
  return TlsRandomNumberGenerator::engine();
}

uint64_t OtelRandom::GenerateRandom64() noexcept
{
  return GetRandomNumberGenerator()();
}

} // namespace yb
