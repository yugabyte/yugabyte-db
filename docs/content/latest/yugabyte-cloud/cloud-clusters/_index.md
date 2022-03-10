---
title: Manage clusters
headerTitle: Manage clusters
linkTitle: Manage clusters
description: Manage your Yugabyte Cloud clusters.
image: /images/section_icons/architecture/core_functions/universe.png
headcontent: Scale clusters, configure backups and maintenance windows, and pause or delete clusters.
section: YUGABYTE CLOUD
menu:
  latest:
    identifier: cloud-clusters
    weight: 150
---

Yugabyte Cloud provides the following tools to manage clusters:

Horizontal and vertical scaling
: To ensure the cluster configuration matches its performance requirements, [scale the cluster](configure-clusters/) vertically or horizontally as your requirements change.

Backups
: Configure a regular [backup](backup-clusters/) schedule, run manual backups, and review previous backups.

Maintenance windows
: Yugabyte performs cluster maintenance, including database upgrades, during scheduled [maintenance windows](cloud-maintenance/).

PostgreSQL extensions
: Extend the functionality of your cluster using [PostgreSQL extensions](add-extensions/).

Pause, resume, and delete
: To reduce costs on unused clusters, you can pause or delete them. Deleting a cluster deletes all of its data, including backups.
: Paused clusters are not billed for instance vCPU capacity. Disk and backup storage are charged at the standard rate (refer to [Cluster costs](../cloud-admin/cloud-billing-costs/#paused-cluster-costs)). Yugabyte notifies you when a cluster is paused for 30 days.
: You can't change the configuration, or read and write data to a paused cluster. Alerts and backups are also stopped. Existing backups remain until they expire.
: Access **Pause/Resume Cluster** and **Terminate Cluster** via the cluster **More Links** menu, or click the three dots icon for the cluster on the **Clusters** page.

<div class="row">

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

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="cloud-maintenance/">
      <div class="head">
        <img class="icon" src="/images/section_icons/manage/backup.png" aria-hidden="true" />
        <div class="title">Maintenance windows</div>
      </div>
      <div class="body">
        Set up maintenance windows and exclusion periods for cluster upgrades.
      </div>
    </a>
  </div>

</div>
