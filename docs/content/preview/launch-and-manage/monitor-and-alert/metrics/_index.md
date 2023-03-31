---
title: YugabyteDB metrics
headerTitle: Metrics
linkTitle: Metrics
headcontent: Monitor clusters using key metrics
description: Learn about YugabyteDB's database metrics, and how to select and use the metrics relevant to your situation.
menu:
  preview:
    identifier: metrics-overview
    parent: monitor-and-alert
    weight: 100
type: indexpage
showRightNav: true
---

YugabyteDB provides nearly two thousand metrics for monitoring, performance-tuning, and troubleshooting system, table, and tablet issues. Use metrics to monitor and manage clusters, troubleshoot performance issues, and identify bottlenecks.

YugabyteDB exports metrics through various endpoints in JSON, HTML, and Prometheus formats. For more information, see [Metrics endpoints](metrics-endpoints/).

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
| server_type | Describes the type of server that originated the metric, and can be one of the following:<ul><li>`yb_tserver` - YB-TServer</li><li>`yb_master` - YB-Master</li><li>`yb_ycqlserver` - YCQL</li><li>`yb_ysqlserver` - YSQL</li><li>`yb_consensus` - RAFT consensus</li><li>`yb_cdc` - Change Data Capture</li></ul> |
| service_type | The logical service name for a given server type. |
| service_method | Optional. Identifies service methods, which are specific functions performed by the service. |
