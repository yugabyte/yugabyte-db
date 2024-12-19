---
title: xCluster deployments
headerTitle: xCluster deployment
linkTitle: xCluster deployments
description: Deploy unidirectional (master-follower) or bidirectional (multi-master) replication between universes
headContent: Unidirectional (master-follower) and bidirectional (multi-master) replication
menu:
  v2024.1:
    identifier: async-replication
    parent: multi-dc
    weight: 610
type: indexpage
---
By default, YugabyteDB provides synchronous replication and strong consistency across geo-distributed data centers. However, many use cases do not require synchronous replication or justify the additional complexity and operating costs associated with managing three or more data centers. A cross-cluster (xCluster) deployment provides asynchronous replication across two data centers or cloud regions. Using an xCluster deployment, you can use unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between two universes (aka data centers).

For information on xCluster deployment architecture, replication scenarios, and limitations, refer to [xCluster replication](../../../architecture/docdb-replication/async-replication/).

{{<index/block>}}

  {{<index/item
    title="Deploy xCluster"
    body="Set up unidirectional or bidirectional replication."
    href="async-deployment/"
    icon="fa-light fa-copy">}}

  {{<index/item
    title="Deploy transactional xCluster"
    body="Set up transactional unidirectional replication."
    href="async-replication-transactional/"
    icon="fa-light fa-money-from-bracket">}}

{{</index/block>}}
