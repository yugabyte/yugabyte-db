---
title: CDC configuration in YugabyteDB
headerTitle: Advanced configuration
linkTitle: Advanced configuration
description: Advanced configuration of Change Data Capture in YugabyteDB.
headcontent: Change Data Capture in YugabyteDB
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    parent: explore-change-data-capture
    identifier: cdc-configuration
    weight: 50
type: docs
---

You can use several flags to fine-tune YugabyteDB's CDC behavior. These flags are documented in the [Change data capture flags](../../../reference/configuration/yb-tserver/#change-data-capture-cdc-flags) section of the YB-TServer reference and [Change data capture flags](../../../reference/configuration/yb-master/#change-data-capture-cdc-flags) section of the YB-Master reference.

## Important configuration settings

The following flags are particularly important for configuring CDC:

- [cdc_intent_retention_ms](../../../reference/configuration/yb-tserver/#cdc-intent-retention-ms) - Controls retention of intents, in ms. If a request for change records is not received for this interval, un-streamed intents are garbage collected and the CDC stream is considered expired. This expiry is not reversible, and the only course of action would be to create a new CDC stream. The default value of this flag is 4 hours (4 x 3600 x 1000 ms).

- [cdc_wal_retention_time_secs](../../../reference/configuration/yb-master/#cdc-wal-retention-time-secs) - Controls how long WAL is retained, in seconds. This is irrespective of whether a request for change records is received or not. The default value of this flag is 4 hours (14400 seconds).

- [cdc_snapshot_batch_size](../../../reference/configuration/yb-tserver/#cdc-snapshot-batch-size) - This flag's default value is 250 records included per batch in response to an internal call to get the snapshot. If the table contains a very large amount of data, you may need to increase this value to reduce the amount of time it takes to stream the complete snapshot. You can also choose not to take a snapshot by modifying the [Debezium](../debezium-connector-yugabytedb/) configuration.

- [cdc_max_stream_intent_records](../../../reference/configuration/yb-tserver/#cdc-max-stream-intent-records) - Controls how many intent records can be streamed in a single `GetChanges` call. Essentially, intents of large transactions are broken down into batches of size equal to this flag, hence this controls how many batches of `GetChanges` calls are needed to stream the entire large transaction. The default value of this flag is 1680, and transactions with intents less than this value are streamed in a single batch. The value of this flag can be increased, if the workload has larger transactions and CDC throughput needs to be increased. Note that high values of this flag can increase the latency of each `GetChanges` call.

## How to retain data for longer durations

To increase retention of data for CDC, change the two flags, `cdc_intent_retention_ms` and `cdc_wal_retention_time_secs` as required.

{{< warning title="Important" >}}

Longer values of `cdc_intent_retention_ms`, coupled with longer CDC lags (periods of downtime where the client is not requesting changes) can result in increased memory footprint in the YB-TServer and affect read performance.

{{< /warning >}}
