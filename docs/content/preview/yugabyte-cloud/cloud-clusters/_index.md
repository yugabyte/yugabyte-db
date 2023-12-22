---
title: How to manage clusters
headerTitle: Manage clusters
linkTitle: Manage clusters
description: Get an overview of how to scale your database clusters, configure backups and maintenance windows, and pause or delete clusters in YugabyteDB Managed.
image: /images/section_icons/architecture/core_functions/universe.png
headcontent: Scale clusters, configure read replicas, backups, and maintenance, and pause clusters
menu:
  preview_yugabyte-cloud:
    parent: yugabytedb-managed
    identifier: cloud-clusters
    weight: 150
type: indexpage
showRightNav: true
---

YugabyteDB Managed provides the following tools to manage clusters:

- [Scaling](configure-clusters/) - To ensure the cluster configuration matches its performance requirements, scale the cluster vertically or horizontally as your requirements change.
- [Read replicas](managed-read-replica/) - Add read replicas to lower read latencies in regions that are distant from your primary cluster.
- [Backups](backup-clusters/) - Configure a regular backup schedule, run manual backups, and review previous backups.
- [Maintenance windows](cloud-maintenance/) - Yugabyte only performs cluster maintenance, including database upgrades, during a weekly maintenance window that you configure.
- [PostgreSQL extensions](add-extensions/) - Extend the functionality of your cluster using PostgreSQL extensions.

### Pause, resume, and delete clusters

To reduce costs on unused clusters, you can pause or delete them.

Access **Pause/Resume Cluster** and **Terminate Cluster** via the cluster **Actions** menu, or click the three dots icon for the cluster on the **Clusters** page.

Deleting a cluster deletes all of its data, including backups.

Paused clusters are not billed for instance vCPU capacity. Disk and backup storage are charged at the standard rate (refer to [Cluster costs](../cloud-admin/cloud-billing-costs/#paused-cluster-costs)). You can't change the configuration, or read and write data to a paused cluster. Alerts and backups are also stopped. Existing backups remain until they expire. You can't pause a Sandbox cluster. Yugabyte notifies you when a cluster is paused for 30 days.

Note that if another [locking operation](#locking-operations) is in progress, you can't pause or delete a cluster until the other operation completes.

### Locking operations

The following operations lock the cluster and only one can happen at the same time:

- [backup and restore](backup-clusters/)
- pause and resume
- [scaling the cluster](configure-clusters/), including adding and removing nodes, increasing disk size, and changing IOPS
- create, delete, and edit of [read replicas](managed-read-replica/)
- any scheduled [maintenance](cloud-maintenance/), including database upgrades, certificate rotations, and cluster maintenance (a backup is run automatically before a database upgrade)
- [configure metrics export](../cloud-monitor/metrics-export/) on the cluster

In addition, on AWS, any disk modification (size or IOPS) blocks further disk modifications for six hours (this includes a scaling operation that increases the number of vCPUs, as this also increases disk size).

Make sure that you schedule maintenance and backups so that they do not conflict.

<div class="row">

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="configure-clusters/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/linear_scalability.png" aria-hidden="true" />
        <div class="title">Scale clusters</div>
      </div>
      <div class="body">
        Scale clusters horizontally or vertically.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="managed-read-replica/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/planet_scale.png" aria-hidden="true" />
        <div class="title">Read replicas</div>
      </div>
      <div class="body">
        Serve read requests from remote regions.
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

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="add-extensions/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/administer.png" aria-hidden="true" />
        <div class="title">Create extensions</div>
      </div>
      <div class="body">
        Create PostgreSQL extensions in YugabyteDB Managed clusters.
      </div>
    </a>
  </div>

</div>
