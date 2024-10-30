---
title: Grafana dashboard
headerTitle: Grafana dashboard
linkTitle: Grafana dashboard
description: Learn about setting up the community dashboards with Prometheus data source using Grafana.
menu:
  v2.20:
    identifier: observability-grafana
    parent: explore-observability
    weight: 240
type: docs
---

You can create dashboards for your local YugabyteDB clusters using [Grafana](https://grafana.com/grafana/), an open source platform to perform visualization and analytics which lets you add queries and explore metrics to customize your graphs.

To create dashboards, you need to add Prometheus metrics as a data source in Grafana. The visualization it provides gives a better understanding of the health and performance of your YugabyteDB clusters.

In this tutorial, you use Grafana to populate dashboards with the metrics you collect using the [Prometheus integration](../../prometheus-integration/macos/).

## Prerequisite

This tutorial requires a local multi-node universe with an instance of Prometheus displaying the metrics of your YugabyteDB cluster. Use the Prometheus setup you created for the [Prometheus integration](../../prometheus-integration/macos/#setup) example.

## Set up Grafana and add Prometheus as a data source

To set up Grafana with Prometheus as a data source, do the following:

1. Install Grafana and start the server according to the [Grafana documentation](https://grafana.com/docs/grafana/latest/installation/mac/).
1. Open the Grafana UI on <http://localhost:3000>. The default login is `admin`, with a password of `admin`.
1. Follow the steps in [Grafana support for Prometheus](https://prometheus.io/docs/visualization/grafana/) to create a Prometheus data source.

## Create a dashboard

There are different ways to create a dashboard in Grafana. For this tutorial, you use the import option and pick the YugabyteDB community dashboard present under the list of Grafana [Dashboards](https://grafana.com/grafana/dashboards/12620).

1. On the Grafana UI, click the **+** button and choose **Import**.

    ![Grafana import](/images/ce/grafana-add.png)

1. On the next page, enter the YugabyteDB dashboard ID in the **Import via grafana.com** field and click **Load**.

    ![Grafana import](/images/ce/grafana-import.png)

1. On the next screen, provide a name for your dashboard, a unique identifier, and the Prometheus data source. Be sure to choose the data source you created earlier.

    ![Grafana dashboard](/images/ce/graf-dash-details.png)

After clicking Import, you can see the new dashboard with various details, starting with the status of your YB-Master and YB-TServer nodes. The metrics displayed are further categorized by API types (YSQL, YCQL) and by API methods (Insert, Select, Update, and so on).

![Grafana dashboard](/images/ce/graf-server-status.png)

Because this example uses the `CassandraKeyValue` workload generator from the [Prometheus integration](../../prometheus-integration/macos/) page, you can see different YCQL-related metrics in addition to the YB-Master and YB-TServer statuses. The [source code](https://github.com/yugabyte/yugabyte-db/blob/master/java/yb-loadtester/src/main/java/com/yugabyte/sample/apps/CassandraSparkKeyValueCopy.java) of the application uses only `SELECT` statements for reads and `INSERT` statements for writes (aside from the initial `CREATE TABLE`). This means that throughput and latency can be measured using the metrics corresponding to the `SELECT` and `INSERT` statements.

The following illustration shows YCQL OPS and latency metrics:

![Grafana YCQL-OPS](/images/ce/graf-ycql-ops.png "YCQL-OPS")

{{% explore-cleanup-local %}}
