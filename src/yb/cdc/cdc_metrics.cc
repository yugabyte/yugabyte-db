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
#include "yb/cdc/cdc_metrics.h"

#include "yb/util/metrics.h"
#include "yb/util/trace.h"


// CDC Tablet metrics.
// Todo(Rahul): Figure out appropriate aggregation functions for these metrics.
METRIC_DEFINE_coarse_histogram(
    cdc, rpc_payload_bytes_responded, "CDC Bytes Responded",
    yb::MetricUnit::kBytes,
    "Payload size of responses to CDC GetChanges requests (only when records are included)");

METRIC_DEFINE_counter(
    cdc, rpc_heartbeats_responded, "CDC Rpc Heartbeat Count",
    yb::MetricUnit::kRequests,
    "Number of responses to CDC GetChanges requests without a record payload.");

METRIC_DEFINE_gauge_int64(
    cdc, last_read_opid_term, "CDC Last Read OpId (Term)",
    yb::MetricUnit::kOperations,
    "ID of the Last Read Producer Operation from a CDC GetChanges request. Format = term.index");

METRIC_DEFINE_gauge_int64(
    cdc, last_read_opid_index, "CDC Last Read OpId (Index)",
    yb::MetricUnit::kOperations,
    "ID of the Last Read Producer Operation from a CDC GetChanges request. Format = term.index");

METRIC_DEFINE_gauge_int64(
    cdc, last_checkpoint_opid_index, "CDC Last Checkpoint OpId (Index)",
    yb::MetricUnit::kOperations,
    "ID of the Last Checkpoint Sent by Consumer in a CDC GetChanges request. Format = term.index");

METRIC_DEFINE_gauge_uint64(
    cdc, last_read_hybridtime, "CDC Last Read HybridTime.",
    yb::MetricUnit::kMicroseconds,
    "HybridTime of the Last Read Operation from a CDC GetChanges request");

METRIC_DEFINE_gauge_uint64(
    cdc, last_read_physicaltime, "CDC Last Read Physical TIme.",
    yb::MetricUnit::kMicroseconds,
    "Physical Time of the Last Read Operation from a CDC GetChanges request");

METRIC_DEFINE_gauge_uint64(
    cdc, last_checkpoint_physicaltime, "CDC Last Committed Physical Time.",
    yb::MetricUnit::kMicroseconds,
    "Physical Time of the Last Committed Operation on Consumer.");

METRIC_DEFINE_gauge_int64(
    cdc, last_readable_opid_index, "CDC Last Readable OpId (Index)",
    yb::MetricUnit::kOperations,
    "Index of the Last Producer Operation that a CDC GetChanges request COULD read.");

METRIC_DEFINE_gauge_int64(
    cdc, async_replication_sent_lag_micros, "CDC Physical Time Lag Last Sent",
    yb::MetricUnit::kMicroseconds,
    "Lag between commit time of last record polled and last record applied on producer.",
    {0 /* zero means we don't expose it as counter */, yb::AggregationFunction::kMax});

METRIC_DEFINE_gauge_int64(
    cdc, async_replication_committed_lag_micros, "CDC Physical Time Lag Last Committed",
    yb::MetricUnit::kMicroseconds,
    "Lag between last record applied on consumer and producer.",
    {0 /* zero means we don't expose it as counter */, yb::AggregationFunction::kMax});

METRIC_DEFINE_gauge_int64(
    cdcsdk, cdcsdk_sent_lag_micros, "CDCSDK sent Lag",
    yb::MetricUnit::kMicroseconds,
    "Lag between last committed record in the producer and the last sent record.",
    {0 /* zero means we don't expose it as counter */, yb::AggregationFunction::kMax});

METRIC_DEFINE_counter(
    cdcsdk, cdcsdk_traffic_sent, "CDCSDK total traffic sent in bytes.", yb::MetricUnit::kBytes,
    "Total traffic sent in bytes.");

METRIC_DEFINE_counter(
    cdcsdk, cdcsdk_change_event_count, "Total number of change events sent.",
    yb::MetricUnit::kUnits, "Total number of change events sent.");

METRIC_DEFINE_gauge_uint64(
    cdcsdk, cdcsdk_expiry_time_ms, "CDCSDK stream expiry time.",
    yb::MetricUnit::kMilliseconds, "CDCSDK stream expiry time in milliseconds.",
    {0 /* zero means we don't expose it as counter */, yb::AggregationFunction::kMax});

METRIC_DEFINE_gauge_uint64(
    cdcsdk, cdcsdk_last_sent_physicaltime, "CDCSDK Last Read Physical TIme.",
    yb::MetricUnit::kMicroseconds,
    "Physical Time of the Last Read Operation from a CDCSDK GetChanges request",
    {0 /* zero means we don't expose it as counter */, yb::AggregationFunction::kMax});

METRIC_DEFINE_gauge_bool(
    cdc, is_bootstrap_required, "Is Bootstrap Required",
    yb::MetricUnit::kUnits, "Is bootstrap required for the replication universe.");

METRIC_DEFINE_gauge_uint64(
    cdc, last_getchanges_time, "CDC Last GetChanges Physical Time",
    yb::MetricUnit::kMicroseconds,
    "Physical time of the last GetChanges request received from the consumer.",
    {0 /* zero means we don't expose it as counter */, yb::AggregationFunction::kMax});

METRIC_DEFINE_gauge_int64(
    cdc, time_since_last_getchanges, "CDC Physical Time Last GetChanges",
    yb::MetricUnit::kMicroseconds,
    "Physical time ellapsed since the last GetChanges request received from the consumer.",
    {0 /* zero means we don't expose it as counter */, yb::AggregationFunction::kMax});

METRIC_DEFINE_gauge_uint64(
    cdc, last_caughtup_physicaltime, "CDC Last Caught-up Physical Time.",
    yb::MetricUnit::kMicroseconds,
    "Physical Time till which consumer has caught-up with producer.");

// CDC Server Metrics
METRIC_DEFINE_counter(
  server, cdc_rpc_proxy_count, "CDC Rpc Proxy Count", yb::MetricUnit::kRequests,
  "Number of CDC GetChanges requests that required proxy forwarding");



namespace yb {
namespace cdc {

#define MINIT(x) x(METRIC_##x.Instantiate(entity))
#define GINIT(x) x(METRIC_##x.Instantiate(entity, 0))
CDCTabletMetrics::CDCTabletMetrics(const scoped_refptr<MetricEntity>& entity)
    : MINIT(rpc_payload_bytes_responded),
      MINIT(rpc_heartbeats_responded),
      GINIT(last_read_opid_term),
      GINIT(last_read_opid_index),
      GINIT(last_checkpoint_opid_index),
      GINIT(last_read_hybridtime),
      GINIT(last_read_physicaltime),
      GINIT(last_checkpoint_physicaltime),
      GINIT(last_readable_opid_index),
      GINIT(async_replication_sent_lag_micros),
      GINIT(async_replication_committed_lag_micros),
      GINIT(is_bootstrap_required),
      GINIT(last_getchanges_time),
      GINIT(time_since_last_getchanges),
      GINIT(last_caughtup_physicaltime),
      entity_(entity) {}

void CDCTabletMetrics::ClearMetrics() {
  last_read_opid_term->set_value(0);
  last_read_opid_index->set_value(0);
  last_checkpoint_opid_index->set_value(0);
  last_read_hybridtime->set_value(0);
  last_read_physicaltime->set_value(0);
  last_checkpoint_physicaltime->set_value(0);
  last_readable_opid_index->set_value(0);
  async_replication_sent_lag_micros->set_value(0);
  async_replication_committed_lag_micros->set_value(0);
  is_bootstrap_required->set_value(false);
  last_getchanges_time->set_value(0);
  time_since_last_getchanges->set_value(0);
  last_caughtup_physicaltime->set_value(0);
}

CDCSDKTabletMetrics::CDCSDKTabletMetrics(const scoped_refptr<MetricEntity>& entity)
    : GINIT(cdcsdk_sent_lag_micros),
      MINIT(cdcsdk_traffic_sent),
      MINIT(cdcsdk_change_event_count),
      GINIT(cdcsdk_expiry_time_ms),
      GINIT(cdcsdk_last_sent_physicaltime),
      entity_(entity) {}

CDCServerMetrics::CDCServerMetrics(const scoped_refptr<MetricEntity>& entity)
    : MINIT(cdc_rpc_proxy_count),
      entity_(entity) { }
#undef MINIT
#undef GINIT

} // namespace cdc
} // namespace yb
