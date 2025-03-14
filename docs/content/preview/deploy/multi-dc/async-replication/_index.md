---
title: xCluster deployments
headerTitle: xCluster deployment
linkTitle: xCluster
description: Deploy unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between two universes
headContent: Unidirectional (master-follower) and bidirectional (multi-master) replication
menu:
  preview:
    identifier: async-replication
    parent: multi-dc
    weight: 610
type: indexpage
---
By default, YugabyteDB provides synchronous replication and strong consistency across geo-distributed data centers. However, many use cases do not require synchronous replication or justify the additional complexity and operating costs associated with managing three or more data centers. A cross-cluster (xCluster) deployment provides asynchronous replication across two data centers or cloud regions. Using an xCluster deployment, you can use unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between two universes (aka data centers).

For information on xCluster deployment architecture, replication scenarios, and limitations, refer to [xCluster replication](../../../architecture/docdb-replication/async-replication/).

{{<index/block>}}

  {{<index/item
    title="Deploy transactional xCluster"
    body="Set up transactional unidirectional replication."
    href="async-replication-transactional/"
    icon="fa-light fa-money-from-bracket">}}

  {{<index/item
    title="Deploy non-transactional xCluster"
    body="Set up non-transactional unidirectional or bidirectional replication."
    href="async-deployment/"
    icon="fa-light fa-copy">}}

{{</index/block>}}

## Prerequisites

- If the root certificates for the source and target clusters are different, (for example, the node certificates for target and source nodes were not created on the same machine), copy the `ca.crt` for the source cluster to all target nodes, and vice-versa. If the root certificate for both source and target clusters is the same, you can skip this step.

    Locate the `ca.crt` file for the source cluster on any node at `<base_dir>/certs/ca.crt`. Copy this file to all target nodes at `<base_dir>/certs/xcluster/<replication_id>/`. The `<replication_id>` must be the same as you configured in Step 1.

    Similarly, copy the `ca.crt` file for the target cluster on any node at `<base_dir>/certs/ca.crt` to source cluster nodes at `<base_dir>/certs/xcluster/<replication_id>/`.

- Set the YB-TServer [log_min_seconds_to_retain](../../../../reference/configuration/yb-tserver/#log-min-seconds-to-retain) to 86400 on both Primary and Standby.

    This flag determines the duration for which WAL is retained on the Primary universe in case of a network partition or a complete outage of the Standby universe. Be sure to allocate enough disk space to hold WAL generated for this duration.

    The value depends on how long a network partition or standby cluster outage can be tolerated, and the amount of WAL expected to be generated during that period.

## Best practices

- Monitor CPU and keep its  use below 65%.
- Monitor disk space and keep its use under 65%.
