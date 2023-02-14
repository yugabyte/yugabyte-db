---
title: YugabyteDB metrics
headerTitle: YugabyteDB metrics
linkTitle: Metrics
description: Learn about the many available YugabyteDB database metrics
menu:
  preview:
    identifier: metrics-overview
    parent: monitor-and-alert
    weight: 200
type: docs
---

YugabyteDB provides thousands of metrics. Use the information on this page to better understand your options for selecting and consuming the metrics relevant to your situation. On this page, you'll learn about the available metrics, their syntax and semantics, and how to view them for a specific use case.

## Export YugabyteDB metrics

Each node in a YugabyteDB cluster exports metrics through JSON and Prometheus Exposition Format API endpoint. You can aggregate the metrics from each node to get a cluster-wide view. YugabyteDB Anywhere (YBA) and YugabyteDB Managed (YBM) aggregate a sub-set of relevant metrics from each node and provide an aggregated cluster view.

### Export by logical architecture components

| Component | Description | JSON API endpoints | Prometheus API endpoint |
| :-------- | :---------- | :----------------- | :---------------------------------------- |
| YSQL | YSQL statement and connection metrics. <br/> Starting point for all YSQL query-related heuristics. | | :13000/metrics | :13000/prometheus-metrics |
| YCQL | YCQL statement and connection metrics. <br/> Starting point for all YCQL query-related heuristics. | :12000/metrics | :12000/prometheus-metrics |
| Master | | :7000/metrics | :7000/threadz | :7000/prometheus-metrics |
| TServer | | :9000/metrics | :9000/threadz | :9000/prometheus-metrics |
| Node Exporter | OS and System level metrics collected by Prometheus Node Exporter. <br/> Use to identify resource consumption trends, and correlate DocDB and system metrics to identify bottlenecks. | | :9300/prometheus-metrics |

### Export YSQL query-related metrics

|     | Description | API endpoint |
| :-: | :---------- | :----------- |
| pg_stat_statement | Per Node <br/> Historical view aggregated by normalizing queries. <br/> Equivalent view on YBA/YBM is _Slow Queries_. | :13000/statements |
| pg_stat_activity | Per Node <br/> Current normalized view of queries running on the node. <br/> Equivalent view on YBA/YBM is _Live Queries_. | :13000/rpcz |

## Metric syntax

There are 4 major metric categories:

* **Server** (logical architectural components) metrics per node
* **Table** metrics per node
* **Tablet** metrics per node
* **Cluster** metrics

The majority of latency metrics expose count, sum, total, and quantiles (P99, P95, P50, mean, max), but a many of them may not be supported.

### Server metric syntax

These are identified by `"type": "server"` in the JSON API endpoint, and by an `export_type` of `tserver_export` in the Prometheus Exposition Format API endpoint.

#### Logical service metrics

Service metric names are of the form `<metric_type>_<server_type>_<service_type>_<service_method>`.

**metric_type** (optional) can be one of:

* `handler_latency` is the latency as seen by the logical architecture block.
* `rpcs_in_queue` is the number of RPCs in queue for a service.
* `service_request_bytes` is the number of bytes sent by a service to other services in a request. This metric type is useful in a very limited number of cases.
* `service_response_bytes` is the number of bytes received by a service from other services in a request. This metric type is useful in a very limited number of cases.
* `proxy_request_bytes` is the number of request bytes sent by the proxy in a request to a service.  Anything that a client requires that the local tablet server cannot provide, is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers and awaiting a majority alias consensus to be reached. This metric type is useful in a very limited number of cases.
* `proxy_response_bytes` is the number of response bytes received by the proxy in from a service. Anything that a client requires that the local tablet server cannot provide, is proxied to the correct service, which can be a master (via the master leader) or another tablet server, changes to the followers and awaiting a majority alias consensus to be reached. This metric type is useful in a very limited number of cases.

**server_type** must be one of the following:

* `yb_tserver` - TServer metrics
* `yb_master` - Master metrics
* `yb_ycqlserver` - YCQL metrics
* `yb_ysqlserver` - YSQL metrics
* `yb_consesus` - RAFT consensus metrics
* `yb_cdc` - Change Data Capture metrics
* `yb_server` - ???

**service_type** is the logical service name within the master or tablet server:

* YB_tserver
  * PgClientService
  * TabletServerService
  * TabletServerAdminService
  * RemoteBootstrapService
  * TabletServerBackupService

* YB_master
  * MasterAdmin
  * MasterClient
  * MasterBackupService
  * MasterBackup
  * MasterService
  * MasterReplication
  * MasterDdl
  * MasterCluster

* YB_ysqlserver
  * SQLProcessor

* YB_cqlserver
  * SQLProcessor

* YB_consensus
  * ConsensusService

* YB_CDC
  * CDCService

* YB_Server
  * GenericService

**service_method** (optional) identifies service methods, which are specific functions performed by the service.

#### Other sever metrics

Yugabyte exports other server metrics listed in the comprehensive metrics list which do not conform to the above syntax and semantics.

### Table metric syntax

### Tablet metric syntax

### Cluster metric syntax

