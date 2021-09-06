---
title: Manage clusters
headerTitle: Manage clusters
linkTitle: Manage clusters
description: Manage your Yugabyte Cloud clusters.
image: /images/section_icons/architecture/core_functions/universe.png
headcontent: Add, monitor, scale, and back up your clusters.
menu:
  latest:
    identifier: cloud-clusters
    parent: yugabyte-cloud
    weight: 100
---

To ensure the cluster configuration matches its performance requirements, you can monitor key database performance metrics and scale the cluster vertically or horizontally as your requirements change. You can also change the default backup policy for clusters, perform on-demand backups, authorize the networks that can access clusters, and connect to clusters. You access your clusters from the **Clusters** page.

The **Clusters** page lists your currently provisioned clusters and their status, along with a summary of nodes, cores, and errors. From here you can [add](../cloud-basics/create-clusters/) and delete clusters.

![Yugabyte Cloud Clusters page](/images/yb-cloud/cloud-clusters.png)

To add a cluster, click **Add Cluster**. Refer to [Create a cluster](../cloud-basics/create-clusters/).

To delete a cluster, click the three dots icon for the cluster you want to delete and choose **Terminate Cluster**. Then enter the name of the cluster and click **Delete**. This deletes the cluster and all of its data, including backups.

To manage a cluster, select the cluster in the list to display the cluster [Overview](overview/). Once you've selected a cluster, you can [connect](../cloud-basics/connect-to-clusters/) to it, [view database tables](cluster-tables/) and [cluster nodes](manage-clusters/), access [backups](backup-clusters/), [authorize the networks](../cloud-basics/add-connections/) that can connect, and [scale the cluster](configure-clusters/).

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="overview/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">View key metrics</div>
      </div>
      <div class="body">
        Evaluate cluster performance with time series charts of key metrics.
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
<!--
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="monitor-clusters/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">Monitor performance</div>
      </div>
      <div class="body">
        Monitor performance metrics and view live and slow running queries.
      </div>
    </a>
  </div>
-->
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="backup-clusters/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Back up clusters</div>
      </div>
      <div class="body">
        Perform on-demand backups and restores, and customize the backup policy.
      </div>
    </a>
  </div>

<!--
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="monitor-activity/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/monitoring.png" aria-hidden="true" />
        <div class="title">Monitor activity</div>
      </div>
      <div class="body">
        Monitor cluster activity and review activity details.
      </div>
    </a>
  </div>
-->
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="configure-clusters/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/enterprise/administer.png" aria-hidden="true" />
        <div class="title">Scale clusters</div>
      </div>
      <div class="body">
        Scale clusters horizontally or vertically and authorize the networks that can connect.
      </div>
    </a>
  </div>
</div>
