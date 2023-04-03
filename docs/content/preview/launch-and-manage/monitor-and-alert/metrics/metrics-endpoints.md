---
title: Metrics endpoints and naming conventions
headerTitle: Metrics endpoints
linkTitle: Endpoints
headcontent: Access YugabyteDB metrics
description: Learn about YugabyteDB metrics endpoints.
menu:
  preview:
    identifier: metrics-endpoints
    parent: metrics-overview
    weight: 10
type: docs
---

A YugabyteDB cluster comprises multiple nodes and services, each emitting metrics. Metrics can be for an entire cluster, or for a specific node, table, or tablet, and they can be aggregated into cluster, node, database, and table views. YugabyteDB has four major types of metrics per node: Server, Table, Tablet, and Cluster. Metrics are exported through various endpoints in JSON, HTML, and Prometheus formats, as shown in the following diagram.

![Metrics endpoints](/images/manage/monitor/metrics-endpoints.png)

The following table describes the types of metrics exposed by each endpoint and the URL from which their metrics can be exported.

| Service | Description | JSON | Prometheus |
| :------------ | :---------- | :------- | :--- |
| [YB-Master](../../../../architecture/concepts/yb-master/) | Metrics related to the system catalog, cluster-wide metadata (such as the number of tablets and tables), and cluster-wide operations (table creations/drops, and so on). | `<node-ip>:7000/metrics` | `<node-ip>:7000/prometheus-metrics` |
| [YB-TServer](../../../../architecture/concepts/yb-tserver/) | Metrics related to end-user DML requests (such as table insert), which include tables, tablets, and storage-level metrics (such as Write-Ahead-Logging, and so on). | `<node-ip>:9000/metrics` | `<node-ip>:9000/prometheus-metrics` |
| [YSQL](../../../../api/ysql/) | YSQL query processing and connection metrics, such as throughput and latencies for various operations. | `<node-ip>:13000/metrics` | `<node-ip>:13000/prometheus-metrics` |
| [YCQL](../../../../api/ycql/) | YCQL query processing and connection metrics, such as throughput and latencies for various operations. | `<node-ip>:12000/metrics` | `<node-ip>:12000/prometheus-metrics` |

System-level metrics are not exposed by YugabyteDB and are generally collected using an external tool such as [node_exporter](https://prometheus.io/docs/guides/node-exporter/) if using Prometheus.

If you use products that automate metrics and monitoring (such as YugabyteDB Anywhere and YugabyteDB Managed), node-level metric collection and aggregation to expose key metrics is provided out-of-the-box.
