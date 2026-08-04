---
title: Monitor YugabyteDB Aeon cluster nodes
headerTitle: Monitor nodes
linkTitle: Monitor nodes
description: Monitor the status of your nodes.
headcontent: Monitor the status of your nodes
menu:
  stable_yugabyte-cloud:
    identifier: monitor-nodes
    parent: cloud-monitor
    weight: 620
type: docs
---

Use the **Nodes** tab to review cluster nodes, including their status and activity. The tab displays read/write operations, tablet distribution, memory use, size, and which nodes host [Masters](../../../architecture/key-concepts/#master-server) and [TServers](../../../architecture/key-concepts/#tserver).

![Cluster Nodes tab](/images/yb-cloud/monitor-nodes.png)

For more information on replication in YugabyteDB, refer to the following topics:

- [Data distribution](../../../explore/linear-scalability/data-distribution/)
- [Synchronous replication](../../../architecture/docdb-replication/replication/)
- [Tablet splitting](../../../architecture/docdb-sharding/tablet-splitting/)
