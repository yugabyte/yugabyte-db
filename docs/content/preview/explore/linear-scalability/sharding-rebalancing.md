---
title: Sharding and rebalancing
headerTitle: Sharding and rebalancing
linkTitle: Sharding and rebalancing
description: Table is split into tablets and is moved seamlessly across nodes
headcontent: Split table into tablets seamlessly across nodes
menu:
  preview:
    identifier: scalability-sharding-rebalancing
    parent: explore-scalability
    weight: 10
type: docs
---

[Horizontal scaling](../) is a first-class feature in YugabyteDB. The database has been designed with horizontal scaling by seamless addition of nodes. Data is moved transparently to the new nodes without any service interruption.

YugabyteDB stores table data in tablets. Sharding is the process of splitting and distributing table data into tablets. Horizontal scalability requires transparent sharding of data.

To understand how sharding works, proceed with the following sections.

## Initial cluster setup

Suppose that you have a 3-node RF3 cluster where you are going to store a basic table with an integer as primary key. The data is stored in a single tablet (say `T1`). The table starts off with a single tablet with a tablet leader (node-2) and two followers (replicas on node-1 and node-3) for high availability.

![Cluster and Table](/images/explore/scalability/sharding-cluster-setup.png)

## Add more data

As you add more data into the table, the leaders and followers of tablet `T1` start to grow.

![Add more rows](/images/explore/scalability/sharding-single-tablet-add-data.png)

## Tablet splitting

Once a tablet reaches a threshold size, say it is `4` rows for the purpose of this illustration, the tablet `T1` splits into two by creating a new tablet, `T2`. The tablet splitting is almost instantaneous and the application will not notice it. The newly created tablet will have leaders and followers.

![Split into two](/images/explore/scalability/sharding-single-tablet-split.png)

## Rebalancing

In the previous illustration, `T1` and `T2` are in the same node (`node-2`). YugabyteDB realizes that the leaders are not balanced and automatically distributes the leaders across different nodes. This ensures that the cluster will be used optimally.

![Leader rebalancing](/images/explore/scalability/sharding-leader-rebalancing.png)

{{<note>}}
Rebalancing is done also for followers and not just for leaders.
{{</note>}}

## Completely scaled out

As more data is added to the table, the tablets split further and are rebalanced as needed. With more data added, you would get a distribution like the following illustration:

![completely scaled out](/images/explore/scalability/sharding-fully-scaled.png)

## Learn more

* [Sharding strategies](../../../architecture/docdb-sharding/sharding/)