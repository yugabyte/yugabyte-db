---
title: Disk failure
linkTitle: Disk failure
description: Recover failing disk
aliases:
  - /troubleshoot/nodes/disk-failure/
menu:
  latest:
    parent: troubleshoot-nodes
    weight: 849
isTocNested: true
showAsideToc: true
---

YugabyteDB can be configured to use multiple disks with the [`--fs_data_dirs`](../../reference/configuration/yb-tserver.md) gflag.
This introduces the possibility of disk failure and recovery issues.

## Cluster replication recovery
The tserver automatically detects the disk failure and tries to spread the data on that disk to other healthy nodes in the cluster.
In a single-zone setup with replication factor 3: if you started with 4 nodes or more, 
then there would be at least three nodes left after one failed. 
In this case, re-replication is automatically started if a tserver/disk is down for 10 minutes.

In a multi-zone setup with replication-factor 3: YugabyteDB will try to keep one copy of data per zone. 
In this case, for automatic re-replication of data, a zone needs to have at least two tservers so that if one fails, 
its data can be re-replicated to the other. Thus, this would mean at least a 6 node cluster.

## Failed disk replacement
The procedure to replace a failed disk is in order:

1. Stop the tserver
2. Replace the disk(s) that have failed
3. Restart the tserver

On restart, the tserver will see the new empty disk and start replicating tablets from other nodes.
