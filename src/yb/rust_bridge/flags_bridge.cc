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


#include "yb/rust_bridge/flags_bridge.h"

#include <cstring>
#include <stdexcept>
#include <string>

#include <boost/lexical_cast.hpp>

#include "yb/util/flags.h"
#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/result.h"

#include "common/yb-cpp-host/src/ffi/flags.rs.h"

namespace yb::rust_bridge {

namespace {

// gflags keeps pointers to the name, help, file and storage for the life of the process.
const char* Persist(rust::Str value) {
  return strdup(std::string(value).c_str());
}

template <class T>
void Register(const char* name, const char* help, const char* file, const std::string& value) {
  T parsed;
  if constexpr (std::is_same_v<T, bool>) {
    if (value == "true" || value == "1") {
      parsed = true;
    } else if (value == "false" || value == "0") {
      parsed = false;
    } else {
      throw std::runtime_error("invalid bool default for flag " + std::string(name));
    }
  } else if constexpr (std::is_same_v<T, std::string>) {
    parsed = value;
  } else {
    parsed = boost::lexical_cast<T>(value);
  }
  new google::FlagRegisterer(name, help, file, new T(parsed), new T(parsed));
}

FlagTag ParseTag(const std::string& tag) {
  for (auto value : FlagTagList()) {
    auto enum_name = ToString(value);  // e.g. "kRuntime"
    std::string lowered;
    for (size_t i = 1; i < enum_name.size(); ++i) {
      lowered += static_cast<char>(std::tolower(enum_name[i]));
    }
    if (lowered == tag) {
      return value;
    }
  }
  throw std::runtime_error("unknown flag tag " + tag);
}

google::CommandLineFlagInfo FindFlag(rust::Str name) {
  google::CommandLineFlagInfo info;
  if (!google::GetCommandLineFlagInfo(std::string(name).c_str(), &info)) {
    throw std::runtime_error("no gflag named " + std::string(name));
  }
  return info;
}

}  // namespace

// Flag values cross with invalid UTF-8 replaced: a string flag can hold any bytes.
rust::String GetFlagValue(rust::Str name) {
  return rust::String::lossy(FindFlag(name).current_value);
}

void DefineFlag(
    rust::Str type, rust::Str name, rust::Str default_value, rust::Str help, rust::Str file,
    const rust::Vec<rust::String>& tags) {
  const auto* name_ptr = Persist(name);
  const auto* help_ptr = Persist(help);
  const auto* file_ptr = Persist(file);
  const std::string value(default_value);
  const std::string type_str(type);
  if (type_str == "bool") {
    Register<bool>(name_ptr, help_ptr, file_ptr, value);
  } else if (type_str == "int32") {
    Register<int32_t>(name_ptr, help_ptr, file_ptr, value);
  } else if (type_str == "int64") {
    Register<int64_t>(name_ptr, help_ptr, file_ptr, value);
  } else if (type_str == "uint64") {
    Register<uint64_t>(name_ptr, help_ptr, file_ptr, value);
  } else if (type_str == "double") {
    Register<double>(name_ptr, help_ptr, file_ptr, value);
  } else if (type_str == "string") {
    Register<std::string>(name_ptr, help_ptr, file_ptr, value);
  } else {
    throw std::runtime_error("unsupported flag type " + type_str);
  }
  for (const auto& tag : tags) {
    new flag_tags_internal::FlagTagger(name_ptr, ParseTag(std::string(tag)));
  }
}

std::unique_ptr<FlagSubscription> WatchFlag(rust::Str name, rust::Box<FlagWatcher> watcher) {
  const auto info = FindFlag(name);
  const std::string flag_name = info.name;
  auto shared_watcher = std::make_shared<rust::Box<FlagWatcher>>(std::move(watcher));
  auto registration = RegisterFlagUpdateCallback(
      info.flag_ptr, "rust_flag_watcher_" + std::to_string(reinterpret_cast<uintptr_t>(
                         shared_watcher.get())),
      [flag_name, shared_watcher] {
        google::CommandLineFlagInfo current;
        if (google::GetCommandLineFlagInfo(flag_name.c_str(), &current)) {
          flag_changed(**shared_watcher, rust::String::lossy(current.current_value));
        }
      });
  if (!registration.ok()) {
    throw std::runtime_error(registration.status().ToString());
  }
  return std::make_unique<FlagSubscription>(std::move(*registration));
}

}  // namespace yb::rust_bridge
