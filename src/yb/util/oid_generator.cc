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

class FastRandomNumberGenerator
{
public:
  using result_type = uint64_t;

  FastRandomNumberGenerator() noexcept = default;

  template <class SeedSequence>
  FastRandomNumberGenerator(SeedSequence &seed_sequence) noexcept
  {
    seed(seed_sequence);
  }

  uint64_t operator()() noexcept
  {
    // Uses the xorshift128p random number generation algorithm described in
    // https://en.wikipedia.org/wiki/Xorshift
    auto &state_a = state_[0];
    auto &state_b = state_[1];
    auto t        = state_a;
    auto s        = state_b;
    state_a       = s;
    t ^= t << 23;        // a
    t ^= t >> 17;        // b
    t ^= s ^ (s >> 26);  // c
    state_b = t;
    return t + s;
  }

  // RandomNumberGenerator concept functions required from standard library.
  // See http://www.cplusplus.com/reference/random/mt19937/
  template <class SeedSequence>
  void seed(SeedSequence &seed_sequence) noexcept
  {
    seed_sequence.generate(reinterpret_cast<uint32_t *>(state_.data()),
                           reinterpret_cast<uint32_t *>(state_.data() + state_.size()));
  }

  static constexpr uint64_t min() noexcept { return 0; }

  static constexpr uint64_t max() noexcept { return std::numeric_limits<uint64_t>::max(); }

private:
  std::array<uint64_t, 2> state_{};
};

} // namespace

// Generates a unique 32byte id, based on uuid v4.
// This class is thread safe

std::string GenerateObjectId(bool binary_id) {
  static Generator generator;
  return generator.Next(binary_id);
}

uint64_t GenerateRandomId() {
  static FastRandomNumberGenerator generator;
  return generator();
}

} // namespace yb
