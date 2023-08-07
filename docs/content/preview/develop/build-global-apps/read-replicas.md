---
title: Read Replicas for global applications
headerTitle: Read replicas
linkTitle: Read replicas
description: Reducing Read Latency for global applications
headcontent: Reduce read latency for global applications
menu:
  preview:
    identifier: global-apps-read-replicas
    parent: build-global-apps
    weight: 900
rightNav:
  hideH3: true
  hideH4: true
type: docs
---

For a highly available system, it is typical to opt for a [Global database](../global-database) that spans multiple regions with preferred leaders set to a specific region. This is great for applications that are running in that region as all the reads would go to the leader located locally.

But applications running in other regions would have to incur cross-region latency to read the latest data from the leader. If a little staleness for reads is okay for the applications running in the other regions, then **Read Replicas** is the pattern to adopt.

A read replica cluster is a set of follower nodes connected to a primary cluster. These are purely observer nodes - which means that they will not take part in the RAFT consensus and elections. This enables the Read Replica cluster to have a different replication factor than the primary cluster (even numbers OK!)

Let's look into how this can be beneficial for your application.

{{<tip>}}
Multiple application instances are active and some instances read stale data.
{{</tip>}}

## Overview

{{<cluster-setup-tabs>}}

Let's say you have your RF3 cluster set up across in `us-east-1` and `us-east-2`,  with leader preference set to `us-east-1`. Let's say that you want to run other applications in `us-central` and `us-west`. Then the read latencies would be similar to the following illustration.

![Read Replicas - setup](/images/develop/global-apps/global-apps-read-replicas-setup.png)

## Improve read latencies

You can set up separate **Read Replica** clusters in each of the regions where you want to run your application where a little staleness is OK.

This will enable the application to read data from the closest replica instead of going cross-region to the tablet leaders.

![Read Replicas - Reduced latency](/images/develop/global-apps/global-apps-read-replicas-final.png)

Notice that the read latency for the application in `us-west` has drastically dropped to 2 ms from the initial 60 ms and the read latency of the application in `us-central` has also dropped to 2 ms.

As replicas may not be up-to-date with all updates, by design, this might return slightly stale data (default: 30s).

{{<note>}}
This is only for reads. All writes still go to the leader.
{{</note>}}

## Failover

When the read replicas in a region fail, the application will redirect its read to the next closest read replica or leader.

![Read Replicas - Failover](/images/develop/global-apps/global-apps-read-replicas-failover.png)

Notice how the application in `us-west` reads from the follower in `us-central` when the read replicas in `us-west` have failed. Even now, the read latency is just 40 ms, much less than the original 60 ms.

## Learn more

- [Read replica architecture](../../../architecture/docdb-replication/read-replicas)