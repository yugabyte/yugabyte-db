---
title: xCluster deployments
headerTitle: xCluster deployment
linkTitle: xCluster
description: Deploy unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between two universes
headContent: Unidirectional (master-follower) and bidirectional (multi-master) replication
menu:
  v2.20:
    identifier: async-replication
    parent: multi-dc
    weight: 610
type: indexpage
---
By default, YugabyteDB provides synchronous replication and strong consistency across geo-distributed data centers. However, many use cases do not require synchronous replication or justify the additional complexity and operating costs associated with managing three or more data centers. A cross-cluster (xCluster) deployment provides asynchronous replication across two data centers or cloud regions. Using an xCluster deployment, you can use unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between two universes (aka data centers).

For information on xCluster deployment architecture and replication scenarios, refer to [xCluster architecture](../../../architecture/docdb-replication/async-replication/).

Before deploying xCluster, review the [limitations](../../../architecture/docdb-replication/async-replication/#limitations).

{{<index/block>}}

  {{<index/item
    title="Deploy transactional xCluster"
    body="Set up transactional unidirectional replication."
    href="async-replication-transactional/"
    icon="fa-thin fa-money-from-bracket">}}

  {{<index/item
    title="Deploy non-transactional xCluster"
    body="Set up non-transactional unidirectional or bidirectional replication."
    href="async-deployment/"
    icon="fa-thin fa-copy">}}

{{</index/block>}}
