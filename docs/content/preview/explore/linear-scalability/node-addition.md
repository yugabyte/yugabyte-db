---
title: Horizontal scaling by adding new nodes
headerTitle: Scale out by adding new nodes
linkTitle: Adding nodes
description: Seamlessly scale and load balance your cluster on-demand
headcontent: Seamlessly scale and load balance your cluster on-demand
menu:
  preview:
    identifier: scalability-node-addition
    parent: explore-scalability
    weight: 20
rightNav:
  hideH3: true
type: docs
---

Seamless node addition is the foundation of scalability. In YugabyteDB, you can scale your cluster horizontally on demand by adding new nodes, as and when needed without any interruption to your applications. Let's go over what happens when a node is added to an existing cluster with a simple illustration.

### Cluster setup

Suppose you have a 3-node cluster with 4 tablets and a replication factor (RF) of 3. You notice that the cluster is not balanced, with one node getting more traffic, and decide to add another node to the cluster.

![Initial setup](/images/explore/scalability/node-addition-cluster-setup.png)

{{<tip>}}
To understand how tablets are formed and split, see [Sharding & Rebalancing](../sharding-rebalancing/).
{{</tip>}}

## New follower creation

When a node is added, the tablets are automatically [rebalanced](../sharding-rebalancing/#rebalancing). The process starts by adding a replica for a tablet in the new node, and the new tablet bootstraps its data from the tablet leader. During this process, throughput is not affected as the data bootstrapping is asynchronous.

In the illustration you can see that a new follower for tablet `T4` is added on the newly added node, `NODE-4` and that follower starts bootstrapping from the leader in `NODE-3`.

![Add a new replica](/images/explore/scalability/node-addition-replication.png)

## Leader switching

After the new replica has been fully bootstrapped, leader election is triggered for the tablet, with a hint to make the replica in the newly added node the leader. This is done to ensure that the leaders are evenly distributed across the various nodes in the cluster. This leader switch is very fast.

In the illustration, you can see that the follower in `NODE-4` has been made the leader. Now each node has `1` tablet leader. Now that node 4 has a tablet leader, it can actively take on load, thereby reducing the load on other nodes.

![New leader](/images/explore/scalability/node-addition-new-leader.png)

## Fix over-replication

As new followers are created, some tablets could be over-replicated. The system automatically removes one extra copy of data.

In the illustration, a new replica of tablet `T4` was created. Now `T4` has 4 copies, although the cluster is RF3. This over-replication is fixed by dropping one of the other replicas.

![Drop replicas](/images/explore/scalability/node-addition-dropping-replicas.png)

## Rebalance followers

Now that the leaders have moved to the new node and the over-replication has been fixed, followers are also re-distributed evenly across the cluster. This is to ensure that the load on all nodes would reasonably be the same.

In the illustration, you can see that followers of `T1` and `T2` have been moved to the newly added `NODE-4`. After the rebalancing is done, you should see a reasonable distribution of leaders and followers across your cluster. as follows:

![Leader distribution](/images/explore/scalability/node-addition-complete.png)

The cluster is now scaled out completely.

## Load balancing

Now that you have successfully added a node and scaled your cluster, applications can connect to any node and send queries. But how will your application know about the new nodes? For this, you can use a YugabyteDB [smart driver](../../../drivers-orms/smart-drivers/) in your application. Smart drivers automatically send traffic to newly added nodes when they become active. Although you can use an external load balancer, smart drivers are topology-aware and will fail over correctly when needed.

![Add a smart driver](/images/explore/scalability/node-addition-smart-driver.png)

## Learn more

- Try it out: [Scale out a universe](../scaling-universe/)
- [Smart drivers](../../../drivers-orms/smart-drivers/)
