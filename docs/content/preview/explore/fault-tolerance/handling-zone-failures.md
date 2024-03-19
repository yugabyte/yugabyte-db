---
title: Handling zone failures
headerTitle:  Handling zone failures
linkTitle: Handling zone failures
description: YugabyteDB can handle in-region failure of zones
headcontent: YugabyteDB can handle in-region failure of zones
menu:
  preview:
    identifier: handling-zone-failures
    parent: fault-tolerance
    weight: 5
type: docs
---

YugabyteDB is resilient to a single-zone failure in an RF3 cluster and a 2-zone failure in an RF5 setup. It is mandatory to deploy across multiple zones to survive zone failures. In this section let us see how YugabyteDB is resilient to a zone failure.

## Setup

Consider a setup where YugabyteDB is deployed across 3 zones in a single region(us-east-1). Say it is an RF3 cluster with leaders and followers distributed across the 3 zones with 3 tablets A, B and C.

![Single region, 3 zones](/images/explore/fault-tolerance/single-region-setup.png)

## Zone fails

Let us say that one of your zones, us-east-1b fails. In this case, the connections established by your application to the nodes in us-east-1b start timing out (typical timeout is 15s). But if new connections are attempted, they will immediately fail. As the nodes in the zone have been from the cluster, some tablets will be leaderless. For example, in the following illustration, tablet B has lost its leader.

![Zone failure](/images/explore/fault-tolerance/single-region-zone-failure.png)

## Leader election

All the nodes in the cluster keep constantly pinging each other for a liveness check. Once a node goes offline, it is identified within 3s and a leader election is triggered. This results in the promotion of one of the followers of the offline tablets to leaders. Leader election is very quick and there is no data loss

In the illustration, you can see that the follower of tablet B in zone-a has been elected as the new leader.

![Zone failure](/images/explore/fault-tolerance/zone-failure-leader-election.png)

## Cluster is fully functional

Once new leaders have been elected, there are no leader-less tablets and the cluster becomes fully functional. There is no data loss as the follower that was elected as the leader will have the latest data (guaranteed by RAFT replication). The recovery time is about 3s. But note that the cluster is under-replicated because some of the followers are currently offline.

![Back to normal](/images/explore/fault-tolerance/zone-failure-fully-functional.png)

## Recovery timeline

Let us go over the recovery timeline. From the point a zone outage occurs, it takes about 3s for all requests to succeed as it takes about 3s for the cluster to realize that nodes are offline and complete leader election. Because of default TCP timeouts, connections already established by applications will take about 15s to fail and on reconnect, they will reconnect to other active nodes.

As of now, the cluster will be under-replicated because of the loss of followers. If the failed nodes don't come back online within 15m, the followers will be considered failed and new followers will be created to guarantee the Replication factor (3). This in essence happens only when the failure of nodes is considered to be a long-term failure.

![Recovery timeline](/images/explore/fault-tolerance/zone-failure-recovery-timeline.png)