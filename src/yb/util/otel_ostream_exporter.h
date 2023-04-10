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

#pragma once

#include "opentelemetry/common/spin_lock_mutex.h"
#include "opentelemetry/nostd/type_traits.h"
#include "opentelemetry/sdk/trace/exporter.h"
#include "opentelemetry/sdk/trace/span_data.h"
#include "opentelemetry/version.h"

#include <iostream>
#include <map>
#include <sstream>

namespace yb
{

/**
 * The OStreamSpanExporter exports span data through an ostream
 */
class OStreamSpanExporter final : public opentelemetry::sdk::trace::SpanExporter
{
public:
  /**
   * Create an OStreamSpanExporter. This constructor takes in a reference to an ostream that the
   * export() function will send span data into.
   * The default ostream is set to stdout
   */
  explicit OStreamSpanExporter(const std::string& base_path, const std::string& file_prefix) noexcept;

  std::unique_ptr<opentelemetry::sdk::trace::Recordable> MakeRecordable() noexcept override;

  opentelemetry::sdk::common::ExportResult Export(
      const opentelemetry::nostd::span<std::unique_ptr<opentelemetry::sdk::trace::Recordable>>
          &spans) noexcept override;

  bool Shutdown(
      std::chrono::microseconds timeout = std::chrono::microseconds::max()) noexcept override;

 private:
  std::string base_path_;
  std::string file_prefix_;

  bool is_shutdown_ = false;
  mutable opentelemetry::common::SpinLockMutex lock_;
  bool isShutdown() const noexcept;

  // Mapping status number to the string from api/include/opentelemetry/trace/canonical_code.h
  std::map<int, std::string> statusMap{{0, "Unset"}, {1, "Ok"}, {2, "Error"}};

  // various print helpers
  void printAttributes(
      std::ostream& sout_,
      const std::unordered_map<std::string, opentelemetry::sdk::common::OwnedAttributeValue> &map,
      const std::string prefix = "\n\t");

  void printEvents(std::ostream& sout_, const std::vector<opentelemetry::sdk::trace::SpanDataEvent> &events);

  void printLinks(std::ostream& sout_, const std::vector<opentelemetry::sdk::trace::SpanDataLink> &links);

  void printResources(std::ostream& sout_, const opentelemetry::sdk::resource::Resource &resources);

  void printInstrumentationScope(
      std::ostream& sout_,
      const opentelemetry::sdk::instrumentationscope::InstrumentationScope &instrumentation_scope);
};
}  // namespace yb
