---
title: xCluster replication (2+ regions) in YugabyteDB
headerTitle: xCluster - Native asynchronous replication
linkTitle: xCluster - Asynchronous replication
description: Multi-region deployment using asynchronous replication across multiple data centers.
headContent: Asynchronous replication between independent YugabyteDB universes
aliases:
  - /stable/explore/two-data-centers-linux/
  - /stable/explore/two-data-centers/macos/
  - /stable/explore/multi-region-deployments/asynchronous-replication-ysql/
  - /stable/explore/going-beyond-sql/asynchronous-replication-ycql/
  - /stable/explore/multi-region-deployments/asynchronous-replication-ycql/
menu:
  stable:
    identifier: asynchronous-replication-ysql
    parent: going-beyond-sql
    weight: 350
type: docs
---

By default, YugabyteDB provides synchronous replication and strong consistency across geo-distributed data centers. A cross-universe (xCluster) deployment provides high throughput asynchronous replication across two data centers or cloud regions.

xCluster provides the following benefits:

- __Low write latency__: With synchronous replication, each write must reach a consensus across a majority of data centers. This can add tens or even hundreds of milliseconds of extra latency for writes in a multi-region deployment. Asynchronous replication with xCluster reduces this latency by eliminating the need for immediate consensus across regions.
- __Only two data centers needed__: With synchronous replication, to tolerate the failure of `f` fault domains, you need at least `2f + 1` fault domains. Therefore, to survive the loss of one data center, a minimum of three data centers is required, which can increase operational costs. For more details, see [fault tolerance](../../../architecture/docdb-replication/replication/#fault-tolerance). However, with async replication you can achieve multi-region deployments with only two data centers.
- __Disaster recovery__: By replicating data asynchronously to a secondary region, xCluster provides a robust disaster recovery solution, allowing for quick failover and minimal data loss in case of a regional outage.

xCluster is always active-active, allowing you to read data from any region. It supports both single-master and multi-master deployment options, offering flexibility to balance write performance and consistency guarantees.

Active-active single-master xCluster:
![example of single-master deployment](/images/architecture/replication/active-standby-deployment-new.png)

Active-active multi-master xCluster:
![example of multi-master deployment](/images/architecture/replication/active-active-deployment-new.png)

For more information, see the following:

- [xCluster architecture](../../../architecture/docdb-replication/async-replication/)
- [xCluster vs Geo-Partitioning vs Read Replicas](../../../explore/multi-region-deployments/)
- [Deploy xCluster](../../../deploy/multi-dc/async-replication)
- [Change data capture (CDC)](../../../architecture/docdb-replication/change-data-capture/)
