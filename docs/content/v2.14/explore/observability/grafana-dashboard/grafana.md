---
title: Grafana dashboard
headerTitle: Grafana dashboard
linkTitle: Grafana dashboard
description: Learn about setting up the community dashboards with Prometheus data source using Grafana.
menu:
  v2.14:
    identifier: observability-2-macos
    parent: explore-observability
    weight: 240
type: docs
---

You can create dashboards for your local YugabyteDB clusters using [Grafana](https://grafana.com/grafana/), an open source platform to perform visualization and analytics which lets you add queries and explore metrics to customize your graphs.
To create dashboards, you need to add Prometheus metrics as a data source in Grafana. The visualization it provides gives a better understanding of the health and performance of your YugabyteDB clusters.

This tutorial uses the [yugabyted](../../../../reference/configuration/yugabyted/) local cluster management utility.
In this tutorial, you'll use Grafana to populate dashboards with the metrics you collect with the [Prometheus integration](../../prometheus-integration/macos/).

## Prerequisite

A local instance of Prometheus displaying the metrics of your YugabyteDB cluster is required. To do this,

- Create a local instance of Prometheus and perform the Steps 1-4 listed under [Prometheus integration](../../prometheus-integration/macos/). (Step 5 is optional)

## Set up Grafana and add Prometheus as a data source

- Install Grafana and start the server according to the [Grafana documentation](https://grafana.com/docs/grafana/latest/installation/mac/).
- Open the Grafana UI on <http://localhost:3000>. The default login is `admin`, with a password of `admin`.
- Follow the steps in [Grafana support for Prometheus](https://prometheus.io/docs/visualization/grafana/) to create a Prometheus data source.

## Create a dashboard

There are different ways to create a dashboard in Grafana. For this tutorial, you'll use the import option and pick the YugabyteDB community dashboard present under the list of Grafana [Dashboards](https://grafana.com/grafana/dashboards/12620).

- On the Grafana UI, click the `+` button and choose Import.

  ![Grafana import](/images/ce/grafana-add.png)

- On the next page, enter the YugabyteDB dashboard ID in the `Import via grafana.com` field and click `Load`.

  ![Grafana import](/images/ce/grafana-import.png)

- On the next screen, provide a name for your dashboard, a unique identifier, and the Prometheus data source. Be sure to choose the data source you created earlier.

  ![Grafana dashboard](/images/ce/graf-dash-details.png)

- After clicking import, you can see the new dashboard with various details starting with the status of your master and t-server nodes. The metrics displayed are further categorized by API types (YSQL, YCQL) and by API methods(Insert, Select, Update, and so on).

  ![Grafana dashboard](/images/ce/graf-server-status.png)

- Because this example uses the `CassandraKeyValue` workload generator from the [Prometheus integration](../../prometheus-integration/macos/) page, you can see different YCQL related metrics in addition to the master and t-server statuses. The [source code](https://github.com/yugabyte/yugabyte-db/blob/master/java/yb-loadtester/src/main/java/com/yugabyte/sample/apps/CassandraSparkKeyValueCopy.java) of the application uses only SELECT statements for reads and INSERT statements for writes (aside from the initial CREATE TABLE). This means that throughput and latency can be measured using the metrics corresponding to the SELECT and INSERT statements.
The following is a YCQL OPS and latency metrics:

  ![Grafana YCQL-OPS](/images/ce/graf-ycql-ops.png "YCQL-OPS")

## Clean up (optional)

Optionally, you can shut down the local cluster you created earlier.

```sh
$ ./bin/yugabyted destroy
```
