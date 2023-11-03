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

#pragma once

#include <string>

#include "yb/gen_yrpc/printer.h"

namespace yb {
namespace gen_yrpc {

struct MetricDescriptor {
  std::string name;
  std::string prefix;
  std::string kind;
  std::string extra_args;
  std::string units;
  std::string description;

  Substitutions CreateSubstitutions() const;
};

void GenerateMethodIndexesEnum(
    YBPrinter printer, const google::protobuf::ServiceDescriptor* service);

void GenerateMetricDefines(
    YBPrinter printer, const google::protobuf::FileDescriptor* file,
    const std::vector<MetricDescriptor>& metric_descriptors);

void GenerateMethodAssignments(
    YBPrinter printer, const google::protobuf::ServiceDescriptor* service,
    const std::string &mutable_metric_fmt, bool service_side,
    const std::vector<MetricDescriptor>& metric_descriptors);

} // namespace gen_yrpc
} // namespace yb
