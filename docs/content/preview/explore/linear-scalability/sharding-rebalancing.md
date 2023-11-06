---
title: Sharding and Rebalancing
headerTitle: Sharding and Rebalancing
linkTitle: Sharding and Rebalancing
description: Table is split into tablets and is moved seamlessly across nodes
headcontent: Table is split into tablets and is moved seamlessly across nodes
menu:
  preview:
    identifier: scalability-sharding-rebalancing
    parent: explore-scalability
    weight: 10
type: docs
---

[Horizontal scaling](../) is a first-class feature in YugabyteDB. The database has been designed with horizontal scaling from day 1. The basis of scaling is the seamless addition of nodes. Data is moved transparently to the new nodes without any service interruption. Let us understand how this happens.

YugabyteDB stores table data in tablets. Sharding is the process of splitting and distributing table data into tablets. Horizontal scalability requires transparent sharding of data. To understand how sharding works, let us go over step by step on how it works with some illustrations.

## Initial cluster setup

Let us say that you have a 3-node RF3 cluster where you are going to store a simple table with an integer as primary key. The data will be stored in a single tablet (say `T1`). The table with start off with a single tablet. This tablet will have a tablet leader and 2 followers(replicas) for high availability.

![Cluster and Table](/images/explore/scalability/sharding-cluster-setup.png)

## Add more data

As you add more data into the table, the leaders and and followers of tablet `T1` start to grow.

![Add more rows](/images/explore/scalability/sharding-single-tablet-add-data.png)

## Tablet splitting

Once a tablet reaches a threshold size, let us say it is `4` rows for the purpose of this illustration, the tablet `T1` splits into two by creating a new tablet, `T2`. The tablet splitting is almost instantaneous and the application will not notice it. the newly created tablet will have leaders and followers.

![Split into two](/images/explore/scalability/sharding-single-tablet-split.png)

## Rebalancing

Now, if you notice , `T1` and `T2` are in the same node (`node-2`). YugabyteDB realizes that the leaders are not balanced and automatically distributes the leaders across different nodes. This will ensure that the cluster will be used optimally.

![Leader rebalancing](/images/explore/scalability/sharding-leader-rebalancing.png)

{{<note>}}
Rebalancing is done also for followers and not just for leaders.
{{</note>}}

## Completely scaled out

Now as more data is added to the table, the tablets split further and are rebalanced as needed. With more data added, you would get a distribution like this.

![completely scaled out](/images/explore/scalability/sharding-fully-scaled.png)




## Learn more
