---
title: Prometheus integration
headerTitle: Prometheus integration
linkTitle: Prometheus integration
description: Learn about exporting YugabyteDB metrics and monitoring the cluster with Prometheus.
menu:
  preview:
    identifier: observability-1-macos
    parent: explore-observability
    weight: 235
type: docs
---

You can monitor your local YugabyteDB cluster with a local instance of [Prometheus](https://prometheus.io/), a popular standard for time series monitoring of cloud native infrastructure. YugabyteDB services and APIs expose metrics in the Prometheus format at the `/prometheus-metrics` endpoint. For details on the metrics targets for YugabyteDB, see [Prometheus monitoring](../../../../reference/configuration/default-ports/#prometheus-monitoring).

## Setup

### Download and install Prometheus

[Download Prometheus](https://prometheus.io/download/) and refer to [Get Started with Prometheus](https://prometheus.io/docs/prometheus/latest/getting_started/) for installation instructions.

### Create a universe

Follow the [setup instructions](../../../#set-up-yugabytedb-universe) to start a local multi-node universe.

### Run the YugabyteDB workload generator

Download the [YugabyteDB workload generator](https://github.com/yugabyte/yb-sample-apps) JAR file (`yb-sample-apps.jar`) using the following command:

{{% yb-sample-apps-path %}}

Run the `CassandraKeyValue` workload application in a separate shell.

```sh
java -jar ./yb-sample-apps.jar \
    --workload CassandraKeyValue \
    --nodes 127.0.0.1:9042 \
    --num_threads_read 1 \
    --num_threads_write 1
```

### Prepare Prometheus configuration file

From your Prometheus home directory, create a file `yugabytedb.yml` and add the following:

```yaml
global:
  scrape_interval:     5s # Set the scrape interval to every 5 seconds. Default is every 1 minute.
  evaluation_interval: 5s # Evaluate rules every 5 seconds. The default is every 1 minute.
  # scrape_timeout is set to the global default (10s).

# YugabyteDB configuration to scrape Prometheus time series metrics
scrape_configs:
  - job_name: "yugabytedb"
    metrics_path: /prometheus-metrics
    kubernetes_sd_configs:
      - role: pod    
    relabel_configs:
      # only scrape the pods where "component" label is "yugabytedb"
      - source_labels: [ __meta_kubernetes_pod_label_component ]
        action: keep
        regex: yugabytedb
      # http-ui is common for both - tservers and masters
      - source_labels: [__meta_kubernetes_pod_container_port_name ]
        action: keep
        regex: http-ui|http-ycql-met|http-ysql-met|http-yedis-met
      # Generating "redis_export" type for http-ui ports of tserver/master containers
      - source_labels: [ __meta_kubernetes_pod_container_name, __meta_kubernetes_pod_container_port_name ]
        action: replace
        regex: yb-([^;]+);http-ui
        target_label: export_type
        replacement: ${1}_export
      # Generating "[cql|ysql|redis]]_export" type for |http-ycql-met|http-ysql-met|http-yedis-met ports of tserver containers
      - source_labels: [ __meta_kubernetes_pod_container_name, __meta_kubernetes_pod_container_port_name ]
        action: replace
        regex: yb-tserver;http-ycql-met
        target_label: export_type
        replacement: cql_export
      - source_labels: [ __meta_kubernetes_pod_container_name, __meta_kubernetes_pod_container_port_name ]
        action: replace
        regex: yb-tserver;http-ysql-met
        target_label: export_type
        replacement: ysql_export
      - source_labels: [ __meta_kubernetes_pod_container_name, __meta_kubernetes_pod_container_port_name ]
        action: replace
        regex: yb-tserver;http-yedis-met
        target_label: export_type
        replacement: redis_export
      # Replace IP wit pod name in instance label
      - source_labels: [ __meta_kubernetes_pod_name, __meta_kubernetes_pod_container_port_number ]
        action: replace
        separator: ":"
        target_label: instance
      # Cluster Name ?    
      - target_label: "node_prefix"
        replacement: "cluster-1"
    metric_relabel_configs:
      # Save the name of the metric so we can group_by since we cannot by __name__ directly...
      - source_labels: ["__name__"]
        regex: "(.*)"
        target_label: "saved_name"
        replacement: "$1"
      # The following basically retrofit the handler_latency_* metrics to label format.
      - source_labels: ["__name__"]
        regex: "handler_latency_(yb_[^_]*)_([^_]*)_([^_]*)(.*)"
        target_label: "server_type"
        replacement: "$1"
      - source_labels: ["__name__"]
        regex: "handler_latency_(yb_[^_]*)_([^_]*)_([^_]*)(.*)"
        target_label: "service_type"
        replacement: "$2"
      - source_labels: ["__name__"]
        regex: "handler_latency_(yb_[^_]*)_([^_]*)_([^_]*)(_sum|_count)?"
        target_label: "service_method"
        replacement: "$3"
      - source_labels: ["__name__"]
        regex: "handler_latency_(yb_[^_]*)_([^_]*)_([^_]*)(_sum|_count)?"
        target_label: "__name__"
        replacement: "rpc_latency$4"

```

### Start Prometheus server

Start the Prometheus server from the Prometheus home directory as follows:

```sh
./prometheus --config.file=yugabytedb.yml
```

Open the Prometheus UI at <http://localhost:9090> and then navigate to the **Targets** page under **Status**.

![Prometheus Targets](/images/ce/prom-targets.png)

## Analyze key metrics

On the Prometheus Graph UI, you can plot the read or write throughput and latency for the `CassandraKeyValue` sample application. Because the [source code](https://github.com/yugabyte/yugabyte-db/blob/master/java/yb-loadtester/src/main/java/com/yugabyte/sample/apps/CassandraKeyValue.java) of the application uses only SELECT statements for reads and INSERT statements for writes (aside from the initial CREATE TABLE), you can measure throughput and latency by using the metrics corresponding to the SELECT and INSERT statements.

Paste the following expressions into the **Expression** box and click **Execute** followed by **Add Graph**.

### Throughput

**Read IOPS**

```sh
sum(irate(rpc_latency_count{server_type="yb_cqlserver", service_type="SQLProcessor", service_method="SelectStmt"}[1m]))
```

![Prometheus Read IOPS](/images/ce/prom-read-iops.png)

**Write IOPS**

```sh
sum(irate(rpc_latency_count{server_type="yb_cqlserver", service_type="SQLProcessor", service_method="InsertStmt"}[1m]))
```

![Prometheus Write IOPS](/images/ce/prom-write-iops.png)

### Latency

**Read Latency (in microseconds)**

```sh
avg(irate(rpc_latency_sum{server_type="yb_cqlserver", service_type="SQLProcessor", service_method="SelectStmt"}[1m])) /
avg(irate(rpc_latency_count{server_type="yb_cqlserver", service_type="SQLProcessor", service_method="SelectStmt"}[1m]))
```

![Prometheus Read Latency](/images/ce/prom-read-latency.png)

**Write Latency (in microseconds)**

```sh
avg(irate(rpc_latency_sum{server_type="yb_cqlserver", service_type="SQLProcessor", service_method="InsertStmt"}[1m])) /
avg(irate(rpc_latency_count{server_type="yb_cqlserver", service_type="SQLProcessor", service_method="InsertStmt"}[1m]))
```

![Prometheus Write Latency](/images/ce/prom-write-latency.png)

## What's next?

Set up [Grafana dashboards](../../grafana-dashboard/grafana/) for better visualization of the metrics being collected by Prometheus.
