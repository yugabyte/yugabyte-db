---
title: Resource governance
linkTitle: Resource governance
description: Set limits on use of shared resources by databases in the same cluster.
headcontent: Set limits on use of shared resources by databases in the same cluster
tags:
  feature: early-access
menu:
  stable_yugabyte-cloud:
    identifier: resource-governance
    parent: cloud-clusters
    weight: 110
type: docs
---

Resource governance intelligently shares CPU between databases during contention, while allowing databases to use available capacity when the cluster is underused. This is particularly useful for multi-tenant applications, providing predictable performance isolation for multiple databases in a single cluster, and preventing "noisy-neighbor" issues by ensuring any one database cannot degrade the performance of others.

You configure resource governance on the cluster **Settings > Resource Governance** tab.

![Cluster Resource Governance](/images/yb-cloud/cloud-clusters-governance.png)

To enable resource governance, choose **Enable Resource Governance**.

To disable resource governance, choose **Disable Resource Governance**.

The **Resource Governance** page lists all the YSQL databases in the cluster, along with the following metrics:

- CPU Activity by Database: shows CPU activity for all the databases in the cluster over time.
- CPU Throttling by Database: shows CPU throttling for all the databases in the cluster over time.

To view a breakdown of the total load on a database, click the database in the list.

## Resource governance policy

By default, each database can access up to 100% of available CPU when there is no contention, and this is suitable for most use cases.

To change the ceiling for all databases:

1. Click **Edit Policy**.
1. Enter the maximum.

    Any single database cannot exceed this limit, regardless of how much spare CPU is available.

1. Click **Configure**

## Limitations

- The cluster must be running YugabyteDB v2026.1 or later.
- Resource Governance is only available for YSQL databases; YCQL keyspaces are not included.
