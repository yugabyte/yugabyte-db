---
title: Seamless scaling by adding new nodes
headerTitle: Seamless scaling by adding new nodes
linkTitle: Add nodes
description: Scale your cluster on demand by adding new nodes
headcontent: Scale your cluster on demand by adding new nodes
menu:
  preview:
    identifier: scalability-node-addition
    parent: explore-scalability
    weight: 20
type: docs
---

In YugabyteDB, you can scale your cluster horizontally on demand by adding new nodes, without any interruption to your applications. Let's see what happens when a node is added to an existing cluster.

## Initial cluster

Let us say you have 3-node RF3 cluster with 4 tablets. You notice that that the cluster is not balanced and hence one node gets more traffic. So you decide you add another node to the cluster.

![Initial setup](/images/explore/scalability/node-addition-cluster-setup.png)

{{<tip>}}
To understand how tablets are formed and split, see [Sharding & Rebalancing](./sharding-rebalancing)
{{</tip>}}

## Replication

When a node is added, the tablets are [rebalanced](./sharding-rebalancing#rebalancing). The process starts off by adding an additional replica for a tablet in the new node which bootstraps its data from the leader of the tablet. During the entire process, throughput is not affected as the whole data bootstrapping is asynchronous.

![Add a new replica](/images/explore/scalability/node-addition-replication.png)

## Switching leaders

Now, once the new replica has been fully bootstrapped, leader election is triggered for the tablet with a hint to make the replica in the newly added node as leader. This leader switch is very fast.

![New leader](/images/explore/scalability/node-addition-new-leader.png)

Now that node-4 has a tablet leader in it, it can actively take in load and thereby reducing the load on other nodes.

## Fix over-replication

When we added the new node, new replica of tablet `T4` was created. Now, in an RF3 cluster, `T4` has 4 copies. This over-replication is fixed by dropping one of the other replicas.

![Dropping replicas](/images/explore/scalability/node-addition-dropping-replicas.png)

## Rebalance followers

Now that the leaders have moved to the new node and the over-replication has been fixed, followers are also re-distributed evenly across the cluster.

![Rebalance followers](/images/explore/scalability/node-addition-rebalance-followers.png)

## Fully scaled out cluster

After all the rebalancing is done, you should see a reasonable distribution of leaders and followers across your cluster like this.

![Rebalance followers](/images/explore/scalability/node-addition-complete.png)

Now, your cluster is scaled out completely.

## Load balancing

Now that you have successfully added a node and scaled your cluster, applications can connect to any node and send queries. Nut how will your application know about the new nodes? For this, you can use YugabytedDB Smart driver in your application, which will automatically send traffic to the newly added nodes, once they become active. Although you can hide your cluster behind a load balancer, the Smart drivers understands the topology of your cluster and will failover correctly when needed.

![Rebalance followers](/images/explore/scalability/node-addition-smart-driver.png)

## Learn more

- [Smart driver](../../../drivers-orms/smart-drivers/)
- [Scale out an universe](./scaling-universe)
