# Disaster recovery

This document described features and APIs required to achieve recovery from a region-level outage when using YugabyteDB. The requirements are the following:

1. RTO should be as small as possible.
1. Presence of the disaster recovery solution should not negatively increase update latencies.
1. RPO should be minimized, however non-zero RPO is acceptable.

Based on these requirements, the solution is to deploy two independent clusters in different regions with uni-directional asynchronous data replication (implemented by xCluster feature).

The initial configuration required for the solution is the following:

* There are two clusters (A and B) deployed in different regions.
* xCluster is configured bi-directionally.
* Cluster A is the source and accepts updates from applications.
* Cluster B is the target and do not accept updates (read-only).
* Both A->B and B->A replication is active.
* PITR is enabled for both A and B (used for unplanned failover - see below).

With such configuration, cluster B is on stand-by, so applications can switch to it at any time with minimal RTO. Async nature of replication guarantees that update latencies are not affected by the presence of the stand-by cluster.

## Failover scenarios

There are two distinct cases for failover:

* Planned - switching apps from the source cluster to the target cluster with **zero RPO**. Planned failover is typically initiated during a maintenance window.
* Unplanned - switching apps from the source cluster to the target cluster in case of source cluster failure. Non-zero RPO is expected in this scenario.

### Planned failover flow

Assuming cluster A is the source and cluster B is the target, the following steps are required to perform planned failover process:

1. Stop applications.
1. Wait for pending updates to propagate to B.
1. Switch application connections to B.
1. Resume applications.

As a result, cluster B becomes the source that replicates updates to cluster A. There is no data loss, so the RPO is zero.

### Unplanned failover flow

Assuming cluster A is the source, cluster B is the target, and cluster A becomes unavailable, the following steps are required to perform failover process:

1. Stop applications.
1. Pause both A->B and B->A replication.
1. Get the latest consistent time on B.
1. Restore B to the latest consistent time using PITR.
1. Switch application connections to B.
1. Resume applications.

When cluster A comes back up, the following steps are required:

1. Restore A to the latest consistent time using PITR.
1. Resume both A->B and B->A replication.

At this point, B is the source cluster and A is the target cluster. To switch back to the initial configuration, the planned failover flow can be used.

## APIs

The following APIs are required to support user flows described above.

### Pause replication

Pauses replication by preventing the target cluster from pulling data from the source cluster.

Invoked on either **source** or **target** cluster.

### Resume replication

Resumes replication by allowing the target cluster to pull data from the source cluster.

Invoked on either **source** or **target** cluster.

### Wait for drain

Waits for all pending updates to be replicated from the source cluster to the target cluster.

Invoked on the **target** cluster.

### Get latest consistent time

Gets the latest consistent time for the target cluster.

Invoked on the **target** cluster.

## Github issues

The work is tracked here: [#13532](https://github.com/yugabyte/yugabyte-db/issues/13532)

## Open questions

- Current design relies on the user to make sure that applications issue updates only to one cluster at a time, which is error-prone. Accidentally issuing updates to both clusters concurrently can lead to data conflicts and inconsistent state. Can this be fixed by introducing a concept of a read-only cluster that rejects any updates? One of the challenges with this is that we can't easily switch the source cluster into read-only mode during unplanned failover, due to this cluster being unavailable.
