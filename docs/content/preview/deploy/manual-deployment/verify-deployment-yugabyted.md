---
title: Verify deployment
headerTitle: Verify deployment
linkTitle: 4. Verify deployment
description: How to verify the manual deployment of the YugabyteDB database cluster.
menu:
  preview:
    identifier: deploy-verify-1-yugabyted
    parent: deploy-manual-deployment
    weight: 615
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../verify-deployment-yugabyted/" class="nav-link active">
      yugabyted
    </a>
  </li>
  <li >
    <a href="../verify-deployment/" class="nav-link">
      Manual
    </a>
  </li>
</ul>

You now have a cluster/universe on six nodes with a replication factor of `3`. Assume their IP addresses are `172.151.17.130`, `172.151.17.220`, `172.151.17.140`, `172.151.17.150`, `172.151.17.160`, and `172.151.17.170`. YB-Master servers are running on only the first three of these nodes.

## Monitor your cluster

When you start a cluster using yugabyted, you can monitor the cluster using the YugabyteDB UI, available at [http://127.0.0.1:15433](http://127.0.0.1:15433).

![YugabyteDB UI Cluster Overview](/images/quick_start/quick-start-ui-overview.png)

Upon checking, you'll see that:

* All three nodes are healthy and in the `RUNNING` state.
* The **replication factor** is `3`, indicating that each node maintains a replica of your data that is replicated synchronously with the Raft consensus protocol. This configuration allows your database deployment to tolerate the outage of one node without losing availability or compromising data consistency.

To view more detailed information about the cluster nodes, go to the **Nodes** tab.

![YugabyteDB UI Nodes Dashboard](/images/tutorials/build-and-learn/chpater2-yugabytedb-ui-nodes-tab.png)

The **Number of Tablets** column provides insights into how YugabyteDB [distributes data and workload](../../../architecture/docdb-sharding).

* **Tablets** - YugabyteDB shards your data by splitting tables into tablets, which are then distributed across the cluster nodes. Currently, the cluster splits system-level tables into `9` tablets (see the **Total** column).

* Tablet **Leaders** and **Peers** - each tablet comprises of a tablet leader and set of tablet peers, each of which stores one copy of the data belonging to the tablet. There are as many leaders and peers for a tablet as the replication factor, and they form a Raft group. The tablet **leaders** are responsible for processing read/write requests that require the data belonging to the tablet. *By distributed tablet leaders across the cluster, YugabyteDB is capable of scaling your data and read/write workloads.*

{{< tip title="Doing some math" >}}
In your case, the replication factor is `3` and there are `9` tablets in total. Each tablet has `1` leader and `2` peers.

The tablet leaders are evenly distributed across all the nodes (see the **Leader** column which is `3` for every node). Plus, every node is a peer for a table it's not the leader for, which brings the total number of **Peers** to `6` on every node.

Therefore, with an RF of `3`, you have:

* `9` tablets in total; with
* `9` leaders (because a tablet can have only one leader); and
* `18` peers (as each tablet is replicated twice, aligning with RF=3).
{{< /tip >}}
