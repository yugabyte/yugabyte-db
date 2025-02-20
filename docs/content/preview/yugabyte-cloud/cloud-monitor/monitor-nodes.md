---
title: Monitor YugabyteDB Aeon cluster nodes
headerTitle: Monitor nodes
linkTitle: Monitor nodes
description: Monitor the status of your nodes.
headcontent: Monitor the status of your nodes
menu:
  preview_yugabyte-cloud:
    identifier: monitor-nodes
    parent: cloud-monitor
    weight: 620
type: docs
---

Review cluster nodes using the **Nodes** tab. The tab shows node status and activity, including the read and write operations, tablet distribution, memory and size, and which nodes host [Masters](../../../architecture/key-concepts/#master-server) and [TServers](../../../architecture/key-concepts/#tserver).

![Cluster Nodes tab](/images/yb-cloud/monitor-nodes.png)

For more information on replication in YugabyteDB, refer to the following topics:

- [Data distribution](../../../explore/linear-scalability/data-distribution/)
- [Synchronous replication](../../../architecture/docdb-replication/replication/)
- [Tablet splitting](../../../architecture/docdb-sharding/tablet-splitting/)
