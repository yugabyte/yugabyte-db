---
title: Throughput and latency metrics
headerTitle: Throughput and latency
linkTitle: Throughput+latency metrics
headcontent: Monitor query processing and database IOPS
description: Learn about YugabyteDB's throughput and latency metrics, and how to select and use the metrics.
menu:
  v2.18:
    identifier: throughput
    parent: metrics-overview
    weight: 100
type: docs
---

YugabyteDB supports additional attributes for latency metrics, which enable you to calculate throughput.

These attributes include the following:

| Attribute | Description |
| :--- | :--- |
| `total_count` | The number of times the value of a metric has been measured.
| `min` | The minimum value of a metric across all measurements.
| `mean` | The average value of the metric across all measurements.
| `Percentile_75` | The 75th percentile value of the metric across all measurements.
| `Percentile_95` | The 95th percentile value of the metric across all measurements.
| `Percentile_99` | The 99th percentile of the metric across all metrics measurements.
| `Percentile_99_9` | The 99.9th percentile of the metric across all metrics measurements.
| `Percentile_99_99` | The 99.99th percentile of the metric across all metrics measurements.
| `max` | The maximum value of the metric across all measurements.
| `total_sum` | The aggregate of all the metric values across the measurements reflected in total_count/count.

For example, if `SELECT * FROM table` is executed once and returns 8 rows in 10 microseconds, the `handler_latency_yb_ysqlserver_SQLProcessor_SelectStmt` metric would have the following attribute values: `total_count=1`, `total_sum=10`, `min=10`, `max=10`, and `mean=10`. If the same query is run again and returns in 6 microseconds, then the attributes would be as follows: `total_count=2`, `total_sum=16`, `min=6`, `max=10`, and `mean=8`.

Although these attributes are present in all `handler_latency` metrics, they may not be calculated for all the metrics.

## YSQL query processing

YSQL query processing metrics represent the total inclusive time it takes YugabyteDB to process a YSQL statement after the query processing layer begins execution. These metrics include the time taken to parse and execute the SQL statement, replicate over the network, the time spent in the storage layer, and so on. The preceding metrics do not capture the time to deserialize the network bytes and parse the query.

The following are key metrics for evaluating YSQL query processing. All metrics are counters and units are microseconds.

| Metric (Counter \| microseconds) | Description |
| :--- | :--- |
| `handler_latency_yb_ysqlserver_SQLProcessor_InsertStmt` | Time to parse and execute INSERT statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_SelectStmt` | Time to parse and execute SELECT statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_UpdateStmt` | Time to parse and execute UPDATE statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_BeginStmt` | Time to parse and execute transaction BEGIN statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_CommitStmt` | Time to parse and execute transaction COMMIT statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_RollbackStmt` | Time to parse and execute transaction ROLLBACK statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_OtherStmts` | Time to parse and execute all other statements apart from the preceding ones listed in this table. Includes statements like PREPARE, RELEASE SAVEPOINT, and so on.
| `handler_latency_yb_ysqlserver_SQLProcessor_Transactions` | Time to execute any of the statements in this table.

The YSQL throughput can be viewed as an aggregate across the whole cluster, per table, and per node by applying the appropriate aggregations.

<!-- | Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_ysqlserver_SQLProcessor_InsertStmt` | The time in microseconds to parse and execute INSERT statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_SelectStmt` | The time in microseconds to parse and execute SELECT statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_UpdateStmt` | The time in microseconds to parse and execute UPDATE statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_BeginStmt` | The time in microseconds to parse and execute transaction BEGIN statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_CommitStmt` | The time in microseconds to parse and execute transaction COMMIT statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_RollbackStmt` | The time in microseconds to parse and execute transaction ROLLBACK statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_OtherStmts` | The time in microseconds to parse and execute all other statements apart from the preceding ones listed in this table. This includes statements like PREPARE, RELEASE SAVEPOINT, and so on. |
| `handler_latency_yb_ysqlserver_SQLProcessor_Transactions` | The time in microseconds to execute any of the statements in this table.| -->

## Database IOPS (reads and writes)

The [YB-TServer](../../../../architecture/concepts/yb-tserver/) is responsible for the actual I/O of client requests in a YugabyteDB cluster. Each node in the cluster has a YB-TServer, and each hosts one or more tablet peers.

The following are key metrics for evaluating database IOPS. All metrics are counters and units are microseconds.

| Metric (Counter \| microseconds) | Description |
| :--- | :--- |
| `handler_latency_yb_tserver_TabletServerService_Read` | Time to perform READ operations at a tablet level.
| `handler_latency_yb_tserver_TabletServerService_Write` | Time to perform WRITE operations at a tablet level.

<!-- | Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_tserver_TabletServerService_Read` | Time in microseconds to perform READ operations at a tablet level |
| `handler_latency_yb_tserver_TabletServerService_Write` | Time in microseconds to perform WRITE operations at a tablet level | -->

These metrics can be viewed as an aggregate across the whole cluster, per table, and per node by applying the appropriate aggregations.
