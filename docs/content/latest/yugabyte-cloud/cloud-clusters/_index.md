---
title: Manage clusters
headerTitle: Manage clusters
linkTitle: Manage clusters
description: Manage your Yugabyte Cloud clusters.
image: /images/section_icons/architecture/core_functions/universe.png
headcontent: Add, delete, scale, and back up your clusters.
menu:
  latest:
    identifier: cloud-clusters
    parent: yugabyte-cloud
    weight: 150
---

To ensure the cluster configuration matches its performance requirements, you can [monitor key database performance metrics](../cloud-monitor/) and scale the cluster vertically or horizontally as your requirements change. You can also change the default backup policy for clusters and perform on-demand backups. You access your clusters from the **Clusters** page.

The **Clusters** page lists your currently provisioned clusters and their status, along with a summary of the total number of clusters, nodes, and vCPUs in your cloud. From here you can also [add](../cloud-basics/create-clusters/) and delete clusters.

To _add_ a cluster, click **Add Cluster**. Refer to [Create a cluster](../cloud-basics/create-clusters/).

To _delete_ a cluster, click the three dots icon for the cluster you want to delete and choose **Terminate Cluster**. Then enter the name of the cluster and click **Delete**. This deletes the cluster and all of its data, including backups.

To _manage_ a cluster, select the cluster in the list to display the cluster [Overview](../cloud-monitor/overview). Once you've selected a cluster, you can access [backups](backup-clusters/), and [scale the cluster](configure-clusters/).

<div class="row">

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

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="configure-clusters/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/enterprise/administer.png" aria-hidden="true" />
        <div class="title">Scale clusters</div>
      </div>
      <div class="body">
        Scale clusters horizontally or vertically.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="add-extensions/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/administer.png" aria-hidden="true" />
        <div class="title">Create extensions</div>
      </div>
      <div class="body">
        Create PostgreSQL extensions in Yugabyte Cloud clusters.
      </div>
    </a>
  </div>

</div>
