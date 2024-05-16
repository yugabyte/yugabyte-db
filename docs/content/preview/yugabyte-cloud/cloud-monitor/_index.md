---
title: Alerts and monitoring in YugabyteDB Managed
headerTitle: Alerts and monitoring
linkTitle: Alerts and monitoring
description: Set alerts and monitor your YugabyteDB Managed clusters.
image: /images/section_icons/explore/monitoring.png
headcontent: Set alerts and monitor cluster performance and activity
aliases:
  - /preview/yugabyte-cloud/cloud-monitor/logging-export/
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-monitor
    weight: 100
type: indexpage
---

Use YugabyteDB Managed alerts and monitoring to monitor cluster performance and be notified of potential problems.

- **Alerts**. To be automatically notified of potential problems, enable alerts for cluster, database, and billing criteria. Configure alerts from the [Alerts](cloud-alerts/) page.
- **Performance monitoring**. Monitor database and cluster performance in real time. YugabyteDB Managed provides the following tools to monitor your clusters:

  - [Performance metrics](overview/). The cluster **Overview** and **Performance Metrics** tabs show a variety of performance metrics charted over time. Use cluster performance metrics to ensure the cluster configuration matches its performance requirements, and [scale the cluster vertically or horizontally](../cloud-clusters/configure-clusters/) as your requirements change.
  - [Live queries](cloud-queries-live/). The cluster **Live Queries** tab shows the queries that are currently "in-flight" on your cluster.
  - [Slow queries](cloud-queries-slow/). The cluster **YSQL Slow  Queries** tab shows queries run on the cluster, sorted by running time. Evaluate the slowest running YSQL queries that have been run on the cluster.
  - [Performance advisor](cloud-advisor/). Scan clusters for performance optimizations, including index and schema changes, and detect potentially hot nodes.
  - [Metrics export](metrics-export/). Export metrics to third-party monitoring tools such as Datadog and Grafana Cloud.

  Access performance monitoring from the cluster **Performance** tab.

- **Cluster properties**. View cluster activity, node status, and database properties:

  - Database tables. Use the cluster **Tables** tab to see the cluster tables, and their database or namespace, and size. Note that table size is calculated from the sum of the write ahead logs (WAL) and sorted-string table (SST) files, across all nodes in the cluster. Changes to the database are first recorded to the WAL. Periodically, these logs are written to SST files for longer-term storage. During this process, the data is compressed. When this happens, you may observe a reduction in the total size of tables.
  - Node status. Use the cluster **Nodes** tab to see the nodes in the cluster and their status.
  - [Activity log](monitor-activity/). The cluster **Activity** tab provides a running audit of changes made to the cluster.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-alerts/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">Alerts</div>
      </div>
      <div class="body">
        Enable alerts for cluster performance metrics and billing.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="overview/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">View performance metrics</div>
      </div>
      <div class="body">
        Evaluate cluster performance with time series charts of performance metrics.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-queries-live/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">View live queries</div>
      </div>
      <div class="body">
        Monitor and display current running queries on your cluster.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-queries-slow/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">View slow queries</div>
      </div>
      <div class="body">
        Monitor and display past YSQL queries on your cluster.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-advisor/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/diagnostics.png" aria-hidden="true" />
        <div class="title">Performance advisor</div>
      </div>
      <div class="body">
        Scan your database for potential optimizations.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="monitor-activity/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">Monitor cluster activity</div>
      </div>
      <div class="body">
        Review the activity on your cluster.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="metrics-export/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">Export metrics</div>
      </div>
      <div class="body">
        Export cluster metrics to third-party monitoring tools.
      </div>
    </a>
  </div>

</div>
