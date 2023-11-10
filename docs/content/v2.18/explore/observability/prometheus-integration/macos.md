---
title: Prometheus integration
headerTitle: Prometheus integration
linkTitle: Prometheus integration
description: Learn about exporting YugabyteDB metrics and monitoring the cluster with Prometheus.
menu:
  v2.18:
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
    relabel_configs:
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

    static_configs:
      - targets: ["127.0.0.1:7000", "127.0.0.2:7000", "127.0.0.3:7000"]
        labels:
          export_type: "master_export"

      - targets: ["127.0.0.1:9000", "127.0.0.2:9000", "127.0.0.3:9000"]
        labels:
          export_type: "tserver_export"

      - targets: ["127.0.0.1:12000", "127.0.0.2:12000", "127.0.0.3:12000"]
        labels:
          export_type: "cql_export"

      - targets: ["127.0.0.1:13000", "127.0.0.2:13000", "127.0.0.3:13000"]
        labels:
          export_type: "ysql_export"

      - targets: ["127.0.0.1:11000", "127.0.0.2:11000", "127.0.0.3:11000"]
        labels:
          export_type: "redis_export"
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
