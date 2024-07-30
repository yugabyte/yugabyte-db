---
title: CDC monitoring in YugabyteDB
headerTitle: Monitor
linkTitle: Monitor
description: Monitor Change Data Capture in YugabyteDB.
headcontent: Monitor deployed CDC connectors
aliases:
  - /preview/explore/change-data-capture/cdc-monitor/
menu:
  preview:
    parent: explore-change-data-capture-grpc-replication
    identifier: cdc-monitor
    weight: 20
type: docs
---

## Status of the deployed connector

You can use the rest APIs to monitor your deployed connectors. The following operations are available:

* List all connectors

   ```sh
   curl -X GET localhost:8083/connectors/
   ```

* Get a connector's configuration

   ```sh
   curl -X GET localhost:8083/connectors/<connector-name>
   ```

* Get the status of all tasks with their configuration

   ```sh
   curl -X GET localhost:8083/connectors/<connector-name>/tasks
   ```

* Get the status of the specified task

   ```sh
   curl -X GET localhost:8083/connectors/<connector-name>/tasks/<task-id>
   ```

* Get the connector's status, and the status of its tasks

   ```sh
   curl -X GET localhost:8083/connectors/<connector-name>/status
   ```

## Metrics

In addition to the built-in support for JMX metrics that Zookeeper, Kafka, and Kafka Connect provide, the YugabyteDB source connector provides the following types of metrics.

### CDC Service metrics

Provide information about CDC service in YugabyteDB.

| Metric name | Type | Description |
| :---- | :---- | :---- |
| cdcsdk_change_event_count | `long` | The Change Event Count metric shows the number of records sent by the CDC Service. |
| cdcsdk_traffic_sent | `long` | The number of milliseconds since the connector has read and processed the most recent event. |
| cdcsdk_event_lag_micros | `long` | The LAG metric is calculated by subtracting the timestamp of the latest record in the WAL of a tablet from the last record sent to the CDC connector. |
| cdcsdk_expiry_time_ms | `long` | The time left to read records from WAL is tracked by the Stream Expiry Time (ms). |

### Snapshot metrics

The **MBean** is `debezium.yugabytedb:type=connector-metrics,server=<database.server.name>,task=<task.id>,context=snapshot`.

Snapshot metrics are only available when a snapshot operation is active, or if a snapshot has occurred since the last connector start. The following snapshot metrics are available:

| Metric name | Type | Description |
| :---- | :---- | :---- |
| LastEvent | `string` | The last snapshot event that the connector has read. |
| MilliSecondsSinceLastEvent | `long` | The number of milliseconds since the connector has read and processed the most recent event. |
| TotalNumberOfEventsSeen | `long` | The total number of events that this connector has seen since the last start or metrics reset. |
| NumberOfEventsFiltered | `long` | The number of events that have been filtered by include/exclude list filtering rules configured on the connector. |
| QueueTotalCapacity | `int` | The length the queue used to pass events between the snapshotter and the main Kafka Connect loop. |
| QueueRemainingCapacity | `int` | The free capacity of the queue used to pass events between the snapshotter and the main Kafka Connect loop. |
| SnapshotRunning | `boolean` | Whether the snapshot is currently running. |
| SnapshotPaused | `boolean` | Whether the snapshot was paused one or more times. |
| SnapshotAborted | `boolean` | Whether the snapshot has been aborted. |
| SnapshotCompleted | `boolean` | Whether the snapshot has been completed. |
| SnapshotDurationInSeconds | `long` | The total number of seconds that the snapshot has taken so far, even if not complete. Includes also time when snapshot was paused.|
| SnapshotPausedDurationInSeconds | `long` | The total number of seconds that the snapshot was paused. If the snapshot was paused more than once, this is the cumulative pause time. |
| MaxQueueSizeInBytes | `long` | The maximum buffer of the queue, in bytes. This metric is available if `max.queue.size.in.bytes` is set to a positive long value. |
| CurrentQueueSizeInBytes | `long` | The current volume, in bytes, of records in the queue. |

### Streaming metrics

The **MBean** is `debezium.yugabytedb:type=connector-metrics,server=<database.server.name>,task=<task.id>,context=streaming`.

The following streaming metrics are available:

| Metric name | Type | Description |
| :---- | :---- | :---- |
| LastEvent | `string` | The last streaming event that the connector has read. |
| MilliSecondsSinceLastEvent | `long` | The number of milliseconds since the connector has read and processed the most recent event. |
| TotalNumberOfEventsSeen | `long` | The total number of events that this connector has seen since the last start or metrics reset. |
| TotalNumberOfCreateEventsSeen | `long` | The total number of create events that this connector has seen since the last start or metrics reset. |
| TotalNumberOfUpdateEventsSeen | `long` |The total number of update events that this connector has seen since the last start or metrics reset. |
| TotalNumberOfDeleteEventsSeen | `long` | The total number of delete events that this connector has seen since the last start or metrics reset. |
| NumberOfEventsFiltered | `long` | The total number of events (since the last start or metrics reset) that have been filtered by include/exclude list filtering rules configured on the connector. |
| QueueTotalCapacity | `int` | The length the queue used to pass events between the streamer and the main Kafka Connect loop. |
| QueueRemainingCapacity | `int` | The free capacity of the queue used to pass events between the streamer and the main Kafka Connect loop. |
| Connected | `boolean` | Indicates whether the connector is currently connected to the database server. |
| MilliSecondsBehindSource | `long` | The number of milliseconds between the last change event's timestamp and when the connector processed it. The value incorporates any differences between the clocks on the machines where the database server and the connector are running. |
| SourceEventPosition | `Map<String, String>` | The coordinates of the last received event. |
| LastTransactionId | `string` | Transaction identifier of the last processed transaction. |
| MaxQueueSizeInBytes | `long` | The maximum buffer of the queue in bytes. This metric is available if `max.queue.size.in.bytes` is set to a positive long value. |
| CurrentQueueSizeInBytes | `long` | The current volume, in bytes, of records in the queue. |
