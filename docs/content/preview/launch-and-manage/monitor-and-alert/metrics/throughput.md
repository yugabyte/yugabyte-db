---
title: Throughput and latency metrics
headerTitle: Throughput and latency
linkTitle: Throughput+latency metrics
headcontent: Monitor query processing and database IOPS
description: Learn about YugabyteDB's throughput and latency metrics, and how to select and use the metrics.
menu:
  preview:
    identifier: throughput
    parent: metrics-overview
    weight: 100
type: docs
---

As referenced in the earlier section, YugabyteDB has latency metrics for Tables/Tablets available on port `9000` for Tserver and port `7000` for master. Additionally, YugabyteDb supports query processing and connection metrics on port `13000` for YSQL and on port `12000` for YCQL. Table/Tablet metrics on Master and Tserver have different set of attributes, which enable you to calculate throughput as compared to query processing and connection metrics for YSQL and YCQL. These are described below. 

Latency metrics for Tables and Tablets in JSON format, for example, `handler_latency_yb_tserver_TabletServerService_Read` - latency metrics to perform READ operation at a tablet level, will have this body 
```json
{
  "name": "handler_latency_yb_tserver_TabletServerService_Read",
  "total_count": 14390,
  "min": 104,
  "mean": 171.65,
  "percentile_75": 0,
  "percentile_95": 0,
  "percentile_99": 0,
  "percentile_99_9": 0,
  "percentile_99_99": 0,
  "max": 1655,
  "total_sum": 2470154
}
```

And will include the following attributes:

| Attribute | Description |
| :--- | :--- |
| `total_count` | The number of times the latency of a metric has been measured.
| `min` | The minimum value of the latency for the metric across all measurements.
| `mean` | The average latency for the metric across all measurements.
| `Percentile_75` | The latency at which 75% of the requests were completed successfully for the metric.
| `Percentile_95` | The latency at which 95% of the requests were completed successfully for the metric.
| `Percentile_99` | The latency at which 99% of the requests were completed successfully for the metric.
| `Percentile_99_9` | The latency at which 99.9% of the requests were completed successfully for the metric.
| `Percentile_99_99` | The latency at which 99.99% of the requests were completed successfully for the metric.
| `max` | The maximum value of latency for the metric across all measurements.
| `total_sum` | The aggregate latency for the metrics across all measurements.

For example, if `SELECT * FROM table` is executed once and returns 8 rows in 10 microseconds, the `handler_latency_yb_ysqlserver_SQLProcessor_SelectStmt` metric would have the following attribute values: `total_count=1`, `total_sum=10`, `min=10`, `max=10`, and `mean=10`. If the same query is run again and returns in 6 microseconds, then the attributes would be as follows: `total_count=2`, `total_sum=16`, `min=6`, `max=10`, and `mean=8`.

Although these attributes are present in all latency metrics, they may not be calculated for all the metrics.

Query processing and connection latency metrics for YSQL and YCQL in JSON format, for example, `handler_latency_yb_ysqlserver_SQLProcessor_SelectStmt` - latency metrics to perform READ operation at a tablet level, will have this body
```json
{
    "name": "handler_latency_yb_ysqlserver_SQLProcessor_SelectStmt",
    "count": 5804,
    "sum": 32777094,
    "rows": 11100
}
```
And will include the following attributes:

| Attribute | Description |
| `count` | The number of times the latency of a metric has been measured.
| `sum` | The aggregate latency for the metrics across all measurements.
| `rows` | The total number of table rows impacted by the operation.


## YSQL query processing

YSQL query processing metrics represent the total inclusive time it takes YugabyteDB to process a YSQL statement after the query processing layer begins execution. These metrics include the time taken to parse and execute the SQL statement, replicate over the network, the time spent in the storage layer, and so on. The preceding metrics do not capture the time to deserialize the network bytes and parse the query.

The following are key metrics for evaluating YSQL query processing.

| Metric | Unit | Type | Description |
| :--- | :--- | :--- | :--- |
| `handler_latency_yb_ysqlserver_SQLProcessor_InsertStmt` | microseconds | counter | Time to parse and execute INSERT statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_SelectStmt` | microseconds | counter | Time to parse and execute SELECT statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_UpdateStmt` | microseconds | counter | Time to parse and execute UPDATE statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_BeginStmt` | microseconds | counter | Time to parse and execute transaction BEGIN statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_CommitStmt` | microseconds | counter | Time to parse and execute transaction COMMIT statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_RollbackStmt` | microseconds | counter | Time to parse and execute transaction ROLLBACK statement.
| `handler_latency_yb_ysqlserver_SQLProcessor_OtherStmts` | microseconds | counter | Time to parse and execute all other statements apart from the preceding ones listed in this table. Includes statements like PREPARE, RELEASE SAVEPOINT, and so on.
| `handler_latency_yb_ysqlserver_SQLProcessor_Transactions` | microseconds | counter | Time to execute any of the statements in this table.

The YSQL throughput can be viewed as an aggregate across the whole cluster, per table, and per node by applying the appropriate aggregations.

<!-- | Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_ysqlserver_SQLProcessor_InsertStmt` | microseconds | counter | The time in microseconds to parse and execute INSERT statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_SelectStmt` | microseconds | counter | The time in microseconds to parse and execute SELECT statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_UpdateStmt` | microseconds | counter | The time in microseconds to parse and execute UPDATE statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_BeginStmt` | microseconds | counter | The time in microseconds to parse and execute transaction BEGIN statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_CommitStmt` | microseconds | counter | The time in microseconds to parse and execute transaction COMMIT statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_RollbackStmt` | microseconds | counter | The time in microseconds to parse and execute transaction ROLLBACK statement |
| `handler_latency_yb_ysqlserver_SQLProcessor_OtherStmts` | microseconds | counter | The time in microseconds to parse and execute all other statements apart from the preceding ones listed in this table. This includes statements like PREPARE, RELEASE SAVEPOINT, and so on. |
| `handler_latency_yb_ysqlserver_SQLProcessor_Transactions` | microseconds | counter | The time in microseconds to execute any of the statements in this table.| -->

## YCQL query processing

YCQL query processing metrics represent the total inclusive time it takes YugabyteDB to process a YCQL statement after the query processing layer begins execution. These metrics include the time taken to parse and execute the YCQL statement, replicate over the network, the time spent in the storage layer, and so on. The preceding metrics do not capture the time to deserialize the network bytes and parse the query.

The following are key metrics for evaluating YCQL query processing.

| Metric | Unit | Type | Description |
| :--- | :--- | :--- | :--- |
| `handler_latency_yb_cqlserver_SQLProcessor_SelectStmt` | microseconds | counter | Time to parse and execute SELECT statement.
| `handler_latency_yb_cqlserver_SQLProcessor_InsertStmt` | microseconds | counter | Time to parse and execute INSERT statement.
| `handler_latency_yb_cqlserver_SQLProcessor_DeleteStmt` | microseconds | counter | Time to parse and execute DELETE statement.
| `handler_latency_yb_cqlserver_SQLProcessor_UpdateStmt` | microseconds | counter | Time to parse and execute UPDATE statement.
| `handler_latency_yb_cqlserver_SQLProcessor_OtherStmts` | microseconds | counter | Time to parse and execute all other statements apart from the preceding ones listed in this table.

## Database IOPS (reads and writes)

The [YB-TServer](../../../../architecture/concepts/yb-tserver/) is responsible for the actual I/O of client requests in a YugabyteDB cluster. Each node in the cluster has a YB-TServer, and each hosts one or more tablet peers.

The following are key metrics for evaluating database IOPS.

| Metric | Unit | Type | Description |
| :--- | :--- | :--- | :--- |
| `handler_latency_yb_tserver_TabletServerService_Read` | microseconds | counter | Time to perform READ operations at a tablet level.
| `handler_latency_yb_tserver_TabletServerService_Write` | microseconds | counter | Time to perform WRITE operations at a tablet level.

<!-- | Metrics | Unit | Type | Description |
| :------ | :--- | :--- | :---------- |
| `handler_latency_yb_tserver_TabletServerService_Read` | microseconds | counter | Time in microseconds to perform WRITE operations at a tablet level |
| `handler_latency_yb_tserver_TabletServerService_Write` | microseconds | counter | Time in microseconds to perform READ operations at a tablet level | -->

These metrics can be viewed as an aggregate across the whole cluster, per table, and per node by applying the appropriate aggregations.
