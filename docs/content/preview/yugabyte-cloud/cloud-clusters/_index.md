---
title: How to manage clusters
headerTitle: Manage clusters
linkTitle: Manage clusters
description: Get an overview of how to scale your database clusters, configure backups and maintenance windows, and pause or delete clusters in YugabyteDB Aeon.
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

YugabyteDB Aeon provides the following tools to manage clusters:

| Feature | Description |
| :--- | :--- |
| [Scaling](configure-clusters/) | To ensure the cluster configuration matches its performance requirements, scale the cluster vertically or horizontally as your requirements change. |
| [Read replicas](managed-read-replica/) | Add read replicas to lower read latencies in regions that are distant from your primary cluster. |
| [Backups](backup-clusters/) | Configure a regular backup schedule, run manual backups, and review previous backups. |
| [Maintenance windows](cloud-maintenance/) | Yugabyte only performs cluster maintenance, including database upgrades, during a weekly maintenance window that you configure. |
| [PostgreSQL&nbsp;extensions](add-extensions/) | Extend the functionality of your cluster using PostgreSQL extensions. |

### Pause, resume, and delete clusters

To reduce costs on unused clusters, you can pause or delete them.

Access **Pause/Resume Cluster** and **Terminate Cluster** via the cluster **Actions** menu, or click the three dots icon for the cluster on the **Clusters** page.

Deleting a cluster deletes all of its data, including backups.

Paused clusters are not billed for instance vCPU capacity. Disk and backup storage are charged at the standard rate (refer to [Cluster costs](../cloud-admin/cloud-billing-costs/#paused-cluster-costs)). You can't change the configuration, or read and write data to a paused cluster. Alerts and backups are also stopped. Existing backups remain until they expire. You can't pause a Sandbox cluster. Yugabyte notifies you when a cluster is paused for 30 days.

Note that if another [locking operation](#locking-operations) is in progress, you can't pause or delete a cluster until the other operation completes.

### Locking operations

Cluster infrastructure operations lock the cluster while they are in progress, and only one can happen at the same time. Some operations also require a restart.

| Locking Operation | Subtask | Restart |
| :--- | :--- | :--- |
| [Backup and restore](backup-clusters/) | | |
| Pause and resume | | |
| [Cluster Edit](configure-clusters/) | Add or remove nodes | |
| [Cluster Edit](configure-clusters/) | Change vCPUs, increase disk size, or change IOPS | Yes |
| [Read replica edit](managed-read-replica/) | Create or delete; add or remove nodes | |
| [Read replica edit](managed-read-replica/) | Increase disk size, change IOPS | Yes |
| [Scheduled maintenance](cloud-maintenance/) | Database upgrades, certificate rotations, and cluster maintenance<br>(A backup is run automatically before a database upgrade) | Yes |
| [Metrics export](../cloud-monitor/metrics-export/) | | |
| [Database query logging](../cloud-monitor/logging-export/) | Enable, disable, modify | Yes |
<!--
| [Database audit logging](../cloud-monitor/logging-export/) | Enable, disable, modify | Yes |
-->
Keep in mind the following:

- For clusters with Node, Availability Zone, or Region fault tolerance, and read replicas with a replication factor greater than 1, restarts are rolling. Your database will continue to function normally during infrastructure operations, but these operations can temporarily degrade application performance.

- For clusters with fault tolerance of none, and read replicas with a replication factor of 1, restarts will result in downtime for the cluster or read replica.

- You should schedule infrastructure operations during periods of low traffic.

- On AWS, any disk modification (size or IOPS) blocks further disk modifications for six hours (this includes a scaling operation that increases the number of vCPUs, as this also increases disk size).

- Make sure that you schedule maintenance and backups so that they do not conflict.

&nbsp;

{{<index/block>}}

  {{<index/item
    title="Scale clusters"
    body="Scale clusters horizontally or vertically."
    href="configure-clusters/"
    icon="/images/section_icons/explore/linear_scalability.png">}}

  {{<index/item
    title="Read replicas"
    body="Serve read requests from remote regions."
    href="managed-read-replica/"
    icon="/images/section_icons/explore/planet_scale.png">}}

  {{<index/item
    title="Back up clusters"
    body="Perform on-demand backups and restores, and customize the backup policy."
    href="backup-clusters/"
    icon="/images/section_icons/manage/backup.png">}}

  {{<index/item
    title="Maintenance windows"
    body="Set up maintenance windows and exclusion periods for cluster upgrades."
    href="cloud-maintenance/"
    icon="/images/section_icons/manage/backup.png">}}

  {{<index/item
    title="Create extensions"
    body="Create PostgreSQL extensions in YugabyteDB Aeon clusters."
    href="add-extensions/"
    icon="/images/section_icons/explore/administer.png">}}

{{</index/block>}}
