---
title: YugabyteDB metrics
headerTitle: Metrics
linkTitle: Metrics
headcontent: Monitor clusters using key metrics
description: Learn about YugabyteDB's database metrics, and how to select and use the metrics relevant to your situation.
menu:
  v2024.1:
    identifier: metrics-overview
    parent: monitor-and-alert
    weight: 100
type: indexpage
showRightNav: true
---

YugabyteDB provides nearly two thousand metrics for monitoring, performance-tuning, and troubleshooting system, table, and tablet issues. Use metrics to monitor and manage clusters, troubleshoot performance issues, and identify bottlenecks.

YugabyteDB exports metrics through various endpoints in JSON, HTML, and Prometheus formats. For more information, see [Metric endpoints](#metric-endpoints).

This section describes the most frequently used metrics, along with how you can consume them.

For information on query tuning, refer to [Query tuning](../../../explore/query-1-performance/).

## Frequently used metrics

To learn about some of the categories of metrics and how to use them for your use case, refer to the following. Note that these sections cover only the most frequently used metrics and is not an exhaustive list of all the metrics exported by YugabyteDB.

| Topic | Description |
| :--- | :--- |
| [Throughput and latency](throughput/) | YSQL query processing and database IOPS. |
| [Connections](connections/) | Cumulative number of connections to YSQL backend per node. |
| [Cache and storage subsystems](cache-storage/) | Storage layer IOPS, block cache, bloom filter, SST file, compaction, memtable, and write ahead logging metrics. |
| [Raft and distributed systems](raft-dst/) | Raft operations, throughput, and latencies, clock skew, and remote bootstrap. |
| [YB-Master](ybmaster/) | Table and tablet management. |
| [Replication](replication/) | Replication lag. |

## Metric naming conventions

Metrics use the following naming convention:

```xml
<metric_category>_<server_type>_<service_type>_<service_method>
```

Note that while this naming convention is generally adopted, YugabyteDB exports other server metrics which do not conform to the syntax.

| Category | Description |
| :--- | :--- |
| metric_category | Optional. Can be one of the following:<ul><li>`handler_latency` - The latency seen by the logical architecture block.</li><li>`rpcs_in_queue` - The number of RPCs in the queue for service.</li><li>`service_request_bytes` - The number of bytes a service sends to other services in a request. Beneficial in a very limited number of cases.</li><li>`service_response_bytes` - The number of bytes a service receives from other services in a request. Beneficial in a very limited number of cases.</li><li>`proxy_request_bytes` - The number of request bytes sent by the proxy in a request to a service. Anything a client requires that the local tablet server cannot provide is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers, and awaiting a majority alias consensus to be reached. Beneficial in a very limited number of cases.</li><li>`proxy_response_bytes` - The number of response bytes the proxy receives from a service. Anything a client requires that the local tablet server cannot provide is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers, and awaiting a majority alias consensus to be reached. Beneficial in a very limited number of cases.</li></ul> |
| server_type | Describes the type of server that originated the metric, and can be one of the following:<ul><li>`yb_tserver` - YB-TServer</li><li>`yb_master` - YB-Master</li><li>`yb_ycqlserver` - YCQL</li><li>`yb_ysqlserver` - YSQL</li><li>`yb_consensus` - Raft consensus</li><li>`yb_cdc` - Change Data Capture</li></ul> |
| service_type | The logical service name for a given server type. |
| service_method | Optional. Identifies service methods, which are specific functions performed by the service. |

## Metric endpoints

A YugabyteDB cluster comprises multiple nodes and services, each emitting metrics. Metrics can be for an entire cluster, or for a specific node, table, or tablet, and they can be aggregated into cluster, node, database, and table views. YugabyteDB has four major types of metrics per node: Server, Table, Tablet, and Cluster. Metrics are exported through various endpoints in JSON, HTML, and Prometheus formats, as shown in the following diagram.

![Metrics endpoints](/images/manage/monitor/metrics-endpoints.png)

The following table describes the types of metrics exposed by each endpoint and the URL from which their metrics can be exported.

| Service | Description | JSON | Prometheus |
| :------------ | :---------- | :------- | :--- |
| [YB-Master](../../../architecture/yb-master/) | Metrics related to the system catalog, cluster-wide metadata (such as the number of tablets and tables), and cluster-wide operations (table creations/drops, and so on). | `<node-ip>:7000/metrics` | `<node-ip>:7000/prometheus-metrics` |
| [YB-TServer](../../../architecture/yb-tserver/) | Metrics related to end-user DML requests (such as table insert), which include tables, tablets, and storage-level metrics (such as Write-Ahead-Logging, and so on). | `<node-ip>:9000/metrics` | `<node-ip>:9000/prometheus-metrics` |
| [YSQL](../../../api/ysql/) | YSQL query processing and connection metrics, such as throughput and latencies for various operations. | `<node-ip>:13000/metrics` | `<node-ip>:13000/prometheus-metrics` |
| [YCQL](../../../api/ycql/) | YCQL query processing and connection metrics, such as throughput and latencies for various operations. | `<node-ip>:12000/metrics` | `<node-ip>:12000/prometheus-metrics` |

System-level metrics are not exposed by YugabyteDB and are generally collected using an external tool such as [node_exporter](https://prometheus.io/docs/guides/node-exporter/) if using Prometheus.

If you use products that automate metrics and monitoring (such as YugabyteDB Anywhere and YugabyteDB Aeon), node-level metric collection and aggregation to expose key metrics is provided out-of-the-box.

## Prometheus Endpoint URL Parameters

The `/prometheus-metrics` endpoint supports filtering metrics at both the table and server levels. Note that tablet level is currently unsupported to prevent server overload.

### Version

| Parameter | Available From | Options | Description |
| :-------- | :------------- | :------ | :---------- |
| `version` | 2.20.2.0 | `v1` (default), `v2` | Determines the endpoint version and the type of regex filters it accepts. <ul><li> `v1`: The endpoint only accepts V1 filters. By default, the table level includes aggregated tablet metrics and table metrics, while the server level includes server metrics.</li><li> `v2`: The endpoint only accepts V2 filters. By default, the table level includes aggregated tablet metrics and table metrics, while the server level includes aggregated tablet metrics, aggregated table metrics, and server metrics.</li></ul> |

### V1 filters

| Parameter | Available From | Options | Description |
| :-------- | :------------- | :------ | :---------- |
| `priority_regex` | 2.9.1.0 | `.*` (default), Empty string, Customized&nbsp;regex | Determines which tablet or table type metrics are emitted at the table level. Metrics are emitted if they match `priority_regex`. If `priority_regex=.*`, exposes all metrics at the table level, while an empty string results in no metrics being exposed. |
| `metrics` | 2.7.2.0 | Customized CSV<br>(No default) | Specifies a CSV list of metrics to be scraped, regardless of level. Metrics not in this list will not be scraped. If this parameter is not provided, all metrics are scraped and then filtered by `priority_regex`. |

### V2 filters

| Parameter | Available From | Options | Description |
| :-------- | :------------- | :------ | :---------- |
| `server_allowlist` | 2.20.2.0 | `ALL` (default), `NONE`, Customized regex | Determines which tablet, table, or server type metrics are emitted at the server level. Metrics are emitted if they match `server_allowlist` and do not match `server_blocklist`. |
| `server_blocklist` | 2.20.2.0 | `ALL`,<br>`NONE` (default), Customized&nbsp;regex | Specifies which server-level metrics are blocked. |
| `table_allowlist` | 2.20.2.0 | `ALL` (default), `NONE`, Customized regex | Determines which tablet or table type metrics are emitted at the table level. Metrics are emitted if they match `table_allowlist` and do not match `table_blocklist`. |
| `table_blocklist` | 2.20.2.0 | `ALL`,<br>`NONE` (default), Customized regex | Specifies which table-level metrics are blocked. |

### Other parameters

| Parameter | Available From | Options | Description |
| :-------- | :------------- | :------ | :---------- |
| `max_metric_entries` | 2.21.1.0 | Integer. Default is the value of flag `max_prometheus_metric_entries` | Limits the number of Prometheus metric entries returned in each scrape. |
| `show_help` | 2.18.4.0 | `true`, `false`. Default is the value of flag `export_help_and_type_in_prometheus_metrics` | Determines whether to include each metric's #TYPE and #HELP in the Prometheus metrics output. |
| `reset_histograms` | 2.17.1.0 | `true`, `false`. Default is `true` | If `false`, percentiles are not reset each time `/prometheus-metrics` is fetched. If `true`, percentiles reset with each fetch. |
