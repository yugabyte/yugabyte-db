---
title: Monitor clusters
headerTitle: Monitor clusters
linkTitle: Monitor clusters
description: Monitor your Yugabyte Cloud clusters.
image: /images/section_icons/explore/monitoring.png
headcontent: Monitor cluster performance and activity.
section: YUGABYTE CLOUD
menu:
  latest:
    identifier: cloud-monitor
    weight: 100
---

To ensure the cluster configuration matches its performance requirements, you can monitor key database performance metrics and [scale the cluster vertically or horizontally](../cloud-clusters/configure-clusters/) as your requirements change.

Yugabyte Cloud provides the following tools to monitor your clusters:

- Performance metrics charted over time.
- A list of live queries.
- A list of slow running YSQL queries that have been run on the cluster.
- A list of the tables, databases, and namespaces on the cluster.
- Status of the nodes in the cluster.
- Activity log of changes made to the cluster.

You access your clusters from the **Clusters** page.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="overview/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
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
        <div class="title">View slow YSQL queries</div>
      </div>
      <div class="body">
        Monitor and display past YSQL queries on your cluster.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cluster-tables/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">View database tables</div>
      </div>
      <div class="body">
        View the tables in your databases.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="manage-clusters/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/enterprise/edit_universe.png" aria-hidden="true" />
        <div class="title">View cluster nodes</div>
      </div>
      <div class="body">
        Check the status of cluster nodes.
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

</div>
