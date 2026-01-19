---
title: xCluster deployments
headerTitle: xCluster deployment
linkTitle: xCluster
description: Deploy unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between two universes
headContent: Unidirectional (master-follower) and bidirectional (multi-master) replication
menu:
  v2024.2:
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

## Prerequisites

- If the root certificates for the source and target clusters are different, (for example, the node certificates for target and source nodes were not created on the same machine), copy the `ca.crt` for the source cluster to all target nodes, and vice-versa. If the root certificate for both source and target clusters is the same, you can skip this step.

    1. For each YB-Master and YB-TServer on both the source and target universe, set the flag `certs_for_cdc_dir` to the parent directory (for example, `<home>/xcluster-certs`) where you want to store all the other universe's certificates for replication.
    1. Find the certificate authority file used by the source universe (`ca.crt`). This should be stored in the [--certs_dir](../../../reference/configuration/yb-master/#certs-dir).
    1. Copy this file to each node on the target universe. It needs to be copied to a directory named `<home>/xcluster-certs/xcluster-replication-id` (create the directory if it is not there).
    1. Similarly, copy the `ca.crt` file for the target universe from any target universe node at `--certs_dir` to the source universe nodes at `<home>/xcluster-certs/<xcluster-replication-id>/` (create the directory if it is not there).

- Global objects like users, roles, tablespaces are not managed by xCluster. You must explicitly create and manage these objects on both source and target universes.

## Best practices

- Set the YB-TServer [cdc_wal_retention_time_secs](../../../reference/configuration/all-flags-yb-tserver/#cdc-wal-retention-time-secs) flag to 86400 on both source and target universe.

    This flag determines the duration for which WAL is retained on the source universe in case of a network partition or a complete outage of the target universe. The value depends on how long a network partition of the source cluster or an outage of the target cluster can be tolerated.

- Make sure all YB-Master and YB-TServer flags are set to the same value on both the source and target universes.

- Monitor CPU usage and ensure it remains below 65%. Note that xCluster replication typically incurs a 20% CPU overhead.

- Monitor disk space usage and ensure it remains below 65%. Allocate sufficient disk space to accommodate WALs generated based on the `cdc_wal_retention_time_secs` setting, which is higher than the default [log_min_seconds_to_retain](../../../reference/configuration/yb-tserver/#log-min-seconds-to-retain) value.
