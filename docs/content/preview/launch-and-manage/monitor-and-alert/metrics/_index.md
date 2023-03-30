---
title: YugabyteDB metrics
headerTitle: YugabyteDB Metrics
linkTitle: Metrics
headcontent: Monitor and manage clusters with YugabyteDB's frequently used metrics.
description: Learn about YugabyteDB's database metrics, and how to select and use the metrics relevant to your situation.
menu:
  preview:
    identifier: metrics-overview
    parent: monitor-and-alert
    weight: 100
type: indexpage
---

YugabyteDB provides nearly two thousand metrics for monitoring, performance-tuning, and troubleshooting system, table, and tablet issues.

The following sections describe how you can consume YugabyteDB metrics, and use them to monitor and manage clusters, troubleshoot performance issues, and identify bottlenecks.

{{< note title="Note" >}}

- This page covers only the most frequently used metrics and not an exhaustive list of all the metrics exported by YugabyteDB.
- This page does not cover query tuning. For more details, refer to [Query tuning](../../../explore/query-1-performance/).

{{< /note >}}

## Overview

A YugabyteDB cluster comprises multiple nodes and services, each emitting metrics. Metrics can be for an entire cluster, or for a specific node, table, or tablet, and they can be aggregated into cluster, node, database, and table views. Metrics are exported through various endpoints in JSON, HTML, and Prometheus formats, as shown in the following diagram.

![Metrics endpoints](/images/manage/monitor/metrics-endpoints.png)

The following table includes a brief description of the type of metrics exposed by each endpoint and the URL from which their metrics can be exported.

| Endpoint name | Description | JSON format | Prometheus format |
| :------------ | :---------- | :---------- | :---------------------------------------- |
| [YB-Master](../../../architecture/concepts/yb-master/) | Exposes metrics related to the system catalog, cluster-wide metadata (such as the number of tablets and tables), and cluster-wide operations (table creations/drops, and so on.). | `<node-ip>:7000/metrics` | `<node-ip>:7000/prometheus-metrics` |
| [YB-TServer](../../../architecture/concepts/yb-tserver/) | Exposes metrics related to end-user DML requests (such as table insert), which include tables, tablets, and storage-level metrics (such as Write-Ahead-Logging, and so on.). | `<node-ip>:9000/metrics` | `<node-ip>:9000/prometheus-metrics` |
| [YSQL](../../../api/ysql/) | Exposes the YSQL query processing and connection metrics, such as throughput and latencies for the various operations. | `<node-ip>:13000/metrics` | `<node-ip>:13000/prometheus-metrics` |
| [YCQL](../../../api/ycql/) | Exposes the YCQL query processing and connection metrics (such as throughput and latencies for various operations). | `<node-ip>:12000/metrics` | `<node-ip>:12000/prometheus-metrics` |

{{< note title="Note" >}}

System-level metrics are not exposed by YugabyteDB and are generally collected using an external tool such as [node_exporter](https://prometheus.io/docs/guides/node-exporter/) if using Prometheus. If you use products that automate metrics and monitoring (such as YugabyteDB Anywhere and YugabyteDB Managed), the node-level metric collection and aggregating to expose the key metrics is done out of the box.

{{< /note >}}

### Metric naming conventions

YugabyteDB has four major types of metrics per node: Server, Table, Tablet, and Cluster.

These metric names are generally of the form `<metric_category>_<server_type>_<service_type>_<service_method>` where:

1. `metric_category` (optional) can be one of the following:

    - `handler_latency` - The latency seen by the logical architecture block.
    - `rpcs_in_queue` - The number of RPCs in the queue for service.
    - `service_request_bytes` - The number of bytes a service sends to other services in a request. This metric type is beneficial in a very limited number of cases.
    - `service_response_bytes` - The number of bytes a service receives from other services in a request. This metric type is beneficial in a very limited number of cases.
    - `proxy_request_bytes` - The number of request bytes sent by the proxy in a request to a service. Anything a client requires that the local tablet server cannot provide is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers, and awaiting a majority alias consensus to be reached. This metric type is beneficial in a very limited number of cases.
    - `proxy_response_bytes` - The number of response bytes the proxy receives from a service. Anything a client requires that the local tablet server cannot provide is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers, and awaiting a majority alias consensus to be reached. This metric type is beneficial in a very limited number of cases.

1. `server_type` can be one of the following:

    - `yb_tserver` - YB-TServer metrics
    - `yb_master` - YB-Master metrics
    - `yb_ycqlserver` - YCQL metrics
    - `yb_ysqlserver` - YSQL metrics
    - `yb_consesus` - RAFT consensus metrics
    - `yb_cdc` - Change Data Capture metrics

1. `service_type` is the logical service name for a given server type.

1. `service_method` (optional) identifies service methods, which are specific functions performed by the service.

{{< note title= "Note">}}

While the preceding metric name syntax is generally adopted, YugabyteDB exports other server metrics which do not conform to the syntax.

{{< /note >}}

To learn about some of the categories of metrics and how to use them relevant to your use case, refer to the following:

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="throughput/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-chart-line"></i></div>
        <div class="title">Throughput and latencies</div>
      </div>
      <div class="body">
          Learn about selecting and using throughput and latency metrics.
      </div>
    </a>
  </div>

 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="connections/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-chart-line"></i></div>
        <div class="title">Connections</div>
      </div>
      <div class="body">
          Learn about selecting and using connection metrics.
      </div>
    </a>
  </div>

   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cache-storage/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-chart-line"></i></div>
        <div class="title">Cache and storage subsystems</div>
      </div>
      <div class="body">
          Learn about selecting and using cache and storage subsystem metrics.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="raft-dst/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-chart-line"></i></div>
        <div class="title">Raft and distributed systems</div>
      </div>
      <div class="body">
          Learn about selecting and using raft and distributed system metrics.
      </div>
    </a>
  </div>

 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="ybmaster/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-chart-line"></i></div>
        <div class="title">YB-Master</div>
      </div>
      <div class="body">
          Learn about selecting and using YB-Master metrics.
      </div>
    </a>
  </div>

<div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="replication/">
      <div class="head">
        <div class="icon"><i class="fa-solid fa-chart-line"></i></div>
        <div class="title">Replication</div>
      </div>
      <div class="body">
          Learn about selecting and using replication metrics.
      </div>
    </a>
  </div>

</div>