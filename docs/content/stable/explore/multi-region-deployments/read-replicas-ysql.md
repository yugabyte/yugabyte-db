---
title: Read replicas and follower reads in YugabyteDB YSQL
headerTitle: Read replicas
linkTitle: Read replicas
description: Explore read replicas in YugabyteDB using YSQL
headContent: Replicate data asynchronously to one or more read replica clusters
menu:
  stable:
    name: Read replicas
    identifier: explore-multi-region-deployments-read-replicas-ysql
    parent: explore-multi-region-deployments
    weight: 750
type: docs
---

{{<api-tabs>}}

Applications running in other regions incur cross-region latency to read the latest data from the leader. If a little staleness for reads is acceptable for the applications running in the other regions, then **Read Replicas** is the pattern to adopt.

A read replica cluster is a set of follower nodes connected to a primary cluster. These are purely observer nodes, which means that they don't take part in the [Raft consensus](https://raft.github.io/) and elections. As a result, read replicas can have a different replication factor (RF) than the primary cluster, and you can have an even number of replicas.

Let's look into how this can be beneficial for your application.

## Setup

{{<cluster-setup-tabs>}}

Suppose you have an RF 3 cluster set up in `us-east-1` and `us-east-2`, with leader preference set to `us-east-1`. And suppose you want to run other applications in `us-central` and `us-west`. The read latencies would be similar to the following illustration.

![Read Replicas - setup](/images/develop/global-apps/global-apps-read-replicas-setup.png)

## Improve read latencies

To improve read latencies, set up separate **Read Replica** clusters in each of the regions where you want to run your application and where a little staleness is acceptable.

This enables the application to read data from the closest replica instead of going cross-region to the tablet leaders.

![Read Replicas - Reduced latency](/images/develop/global-apps/global-apps-read-replicas-final.png)

Notice that the read latency for the application in `us-west` has drastically dropped to 2 ms from the initial 60 ms and the read latency of the application in `us-central` has also dropped to 2 ms.

As replicas may not be up-to-date with all updates, by design, this might return slightly stale data (the default is 30 seconds, but this can be configured).

{{<note>}}
This is only for reads. All writes still go to the leader.
{{</note>}}

## Failover

When the read replicas in a region fail, the application will redirect its read to the next closest read replica or leader.

![Read Replicas - Failover](/images/develop/global-apps/global-apps-read-replicas-failover.png)

Notice how the application in `us-west` reads from the follower in `us-central` when the read replicas in `us-west` fail. In this case, the read latency is 40 ms, still much less than the original 60 ms.

## Learn more

- [Read replica architecture](../../../architecture/docdb-replication/read-replicas)
- [Follower reads](../../going-beyond-sql/follower-reads-ysql/)
