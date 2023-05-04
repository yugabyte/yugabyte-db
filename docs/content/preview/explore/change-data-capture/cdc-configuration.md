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

## Important configuration settings
[cdc_intent_retention_ms](../../reference/configuration/yb-tserver#cdcintentretentionms) - This flag controls retnetion of intents, in ms. If we don't receive a request for change records for this interval, unstreamed intents will be garbage collected and the CDC stream will be considered as expired. This expiry is not reversible, and the only course of action would be to create a new CDC stream. The default value of this falg is 4 hours (4 * 3600 * 1000 ms).

[cdc_wal_retention_time_secs](../../reference/configuration/yb-master#cdcwalretentiontimesecs) - This flag controls how long we retain WAL, in terms of seconds. This is irrespective of weather we have received a request for change records or not. The default value of this flag is 4 hours (14400 seconds).

[cdc_snapshot_batch_size](../../reference/configuration/yb-tserver/#cdcsnapshotbatchsize) - This flag's default value is 250 records included per batch in response to an internal call to get the snapshot. If the table contains a very large amount of data, you may need to increase this value to reduce the amount of time it takes to stream the complete snapshot. You can also choose not to take a snapshot by modifying the [Debezium](../change-data-capture/debezium-connector-yugabytedb/) configuration.

[cdc_max_stream_intent_records](../../reference/configuration/yb-tserver/#cdcmaxstreamintentrecords) - This flag controls how many
intent records can be streamed in a single 'GetChanges' call. Essentially, intents of large transactions will be broken down into batches of size equal to this falg, hence this controls how many batches of 'GetChanges' calls are needed to stream the entire large transaction. The default value of this flag is 1680, and transactions with intents less than this value will be streamed in a single batch. The value of this flag can be increased, if the workload has larger transactions and CDC's throughput needs to be increased.
**High values of this flag can increase the latency of each 'GetChanges' call**

## How to retain data for longer durations
To increase retention of data for CDC, the two falgs: cdc_intent_retention_ms and cdc_wal_retention_time_secs have to be changed, as required.

> **Warning**
> Longer values of 'cdc_intent_retention_ms', coupled with longer CDC lags (periods of downtime where the client is not requesting changes) can result in increased memory footprint in the TServer and can affect read performance.


## YB-TServer configuration

There are several flags you can use to fine-tune YugabyteDB's CDC behavior. These flags are documented in the [Change data capture flags](../../../reference/configuration/yb-tserver/#change-data-capture-cdc-flags) section of the yb-tserver reference page.
