---
title: Pause, resume, and delete clusters
linkTitle: Pause, resume, delete
description: Pause, resume, and delete clusters in Yugabyte Cloud.
headcontent:
image: /images/section_icons/manage/backup.png
menu:
  latest:
    identifier: pause-clusters
    parent: cloud-clusters
    weight: 500
isTocNested: true
showAsideToc: true
---

## Pause clusters

To reduce costs on temporarily unused clusters, you can pause a cluster for up to 30 days. Paused clusters are not billed for instance vCPU capacity, and are billed for disk storage at a reduced rate. Backup storage is charged at the standard rate. Refer to [Cluster costs](../../cloud-admin/cloud-billing-costs/).

To pause a cluster, on the **Clusters** page, click the three dots icon for the cluster you want to pause and choose **Pause Cluster**. You can also select the cluster, click **More Links**, and click **Pause Cluster**.

You can't pause a cluster if it was resumed less than an hour before.

Paused clusters resume automatically after 30 days. Yugabyte notifies you when a cluster is paused and resumed.

Paused clusters have the following limitations:

- You can't change the configuration of a paused cluster.
- You can't read and write data to a paused cluster.
- All alerts and backups are stopped for paused clusters. Existing snapshots remain until they expire.

## Resume clusters

To manually resume a paused cluster, on the **Clusters** page, click the three dots icon for the cluster you want to resume and choose **Resume Cluster**. You can also select the cluster and click **Resume**.

## Delete clusters

To delete a cluster, on the **Clusters** page, click the three dots icon for the cluster you want to delete and choose **Terminate Cluster**. Then enter the name of the cluster and click **Delete**. This deletes the cluster and all of its data, including backups.
