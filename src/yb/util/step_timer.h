// Copyright (c) YugabyteDB, Inc.
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

#include <ostream>
#include <string>
#include <utility>

#include <boost/container/small_vector.hpp>

#include "yb/util/monotime.h"

namespace yb {

// Splits the time an operation takes into consecutive steps, so that a log line can say where a
// slow operation spent its time. Logging is left to the caller, so that VLOG and --vmodule apply
// to the caller's file.
class StepTimer {
 public:
  StepTimer() : start_(MonoTime::Now()) {}

  // Ends the current step, if any, and starts the step called name. name must outlive this
  // object. A step lasts until the next one starts, so if the operation returns early, the step
  // that failed is the last one and still gets its time.
  void StartStep(const char* name) {
    steps_.emplace_back(name, MonoTime::Now());
  }

  MonoDelta Elapsed() const {
    return MonoTime::Now() - start_;
  }

  // E.g. "took 12.345s: flush=10.123s, checkpoint=2.201s".
  std::string ToString() const {
    const auto now = MonoTime::Now();
    std::string result = "took " + (now - start_).ToString();
    for (size_t i = 0; i != steps_.size(); ++i) {
      const auto step_end = i + 1 == steps_.size() ? now : steps_[i + 1].second;
      result += i == 0 ? ": " : ", ";
      result += steps_[i].first;
      result += '=';
      result += (step_end - steps_[i].second).ToString();
    }
    return result;
  }

 private:
  const MonoTime start_;
  boost::container::small_vector<std::pair<const char*, MonoTime>, 8> steps_;
};

inline std::ostream& operator<<(std::ostream& out, const StepTimer& timer) {
  return out << timer.ToString();
}

} // namespace yb
