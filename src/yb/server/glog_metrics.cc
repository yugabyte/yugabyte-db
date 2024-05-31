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
#include "yb/server/glog_metrics.h"

#include "yb/util/logging.h"

#include "yb/util/metrics.h"

METRIC_DEFINE_counter(server, glog_info_messages,
                      "INFO-level Log Messages", yb::MetricUnit::kMessages,
                      "Number of INFO-level log messages emitted by the application.");

METRIC_DEFINE_counter(server, glog_warning_messages,
                      "WARNING-level Log Messages", yb::MetricUnit::kMessages,
                      "Number of WARNING-level log messages emitted by the application.");

METRIC_DEFINE_counter(server, glog_error_messages,
                      "ERROR-level Log Messages", yb::MetricUnit::kMessages,
                      "Number of ERROR-level log messages emitted by the application.");

namespace yb {

class MetricsSink : public google::LogSink {
 public:
  explicit MetricsSink(const scoped_refptr<MetricEntity>& entity) :
    info_counter_(METRIC_glog_info_messages.Instantiate(entity)),
    warning_counter_(METRIC_glog_warning_messages.Instantiate(entity)),
    error_counter_(METRIC_glog_error_messages.Instantiate(entity)) {
  }

  virtual void send(google::LogSeverity severity, const char* full_filename,
                    const char* base_filename, int line,
                    const struct ::tm* tm_time,
                    const char* message, size_t message_len) override {

    Counter* c;
    switch (severity) {
      case google::INFO:
        c = info_counter_.get();
        break;
      case google::WARNING:
        c = warning_counter_.get();
        break;
      case google::ERROR:
        c = error_counter_.get();
        break;
      default:
        return;
    }

    c->Increment();
  }

 private:
  scoped_refptr<Counter> info_counter_;
  scoped_refptr<Counter> warning_counter_;
  scoped_refptr<Counter> error_counter_;
};

ScopedGLogMetrics::ScopedGLogMetrics(const scoped_refptr<MetricEntity>& entity)
  : sink_(new MetricsSink(entity)) {
  google::AddLogSink(sink_.get());
}

ScopedGLogMetrics::~ScopedGLogMetrics() {
  google::RemoveLogSink(sink_.get());
}



} // namespace yb
