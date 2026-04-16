---
title: Verify deployment
headerTitle: 4. Verify deployment
linkTitle: 4. Verify deployment
description: How to verify the manual deployment of the YugabyteDB database cluster.
menu:
  v2.25:
    identifier: deploy-verify-1-yugabyted
    parent: deploy-manual-deployment
    weight: 615
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../verify-deployment-yugabyted/" class="nav-link active">
      <img src="/icons/database.svg" alt="Server Icon">
      yugabyted
    </a>
  </li>
  <li >
    <a href="../verify-deployment/" class="nav-link">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>
</ul>

## Monitor your cluster

When you start a cluster using yugabyted, you can monitor the cluster using the YugabyteDB UI, available at `http://<IP_of_VM>:15433`, where IP_of_VM is the IP address of any node in the cluster.

![YugabyteDB UI Cluster Overview](/images/quick_start/quick-start-ui-overview.png)

Upon checking, you'll see that:

* The status of nodes, including their health and state (`RUNNING`).
* The **replication factor** is `3`, indicating that each node maintains a replica of your data that is replicated synchronously with the Raft consensus protocol. This configuration allows your database deployment to tolerate the outage of one node without losing availability or compromising data consistency.

To view more detailed information about the cluster nodes, go to the **Nodes** tab.

![YugabyteDB UI Nodes Dashboard](/images/tutorials/build-and-learn/chpater2-yugabytedb-ui-nodes-tab.png)

The **Number of Tablets** column provides insights into how YugabyteDB [distributes data and workload](../../../architecture/docdb-sharding).

* **Tablets** - YugabyteDB shards your data by splitting tables into tablets, which are then distributed across the cluster nodes (see the **Total** column).

* Tablet **Leaders** and **Peers** - each tablet comprises of a tablet leader and set of tablet peers, each of which stores one copy of the data belonging to the tablet. There are as many leaders and peers for a tablet as the replication factor, and they form a Raft group. The tablet **leaders** are responsible for processing read/write requests that require the data belonging to the tablet. *By distributed tablet leaders across the cluster, YugabyteDB is capable of scaling your data and read/write workloads.*
