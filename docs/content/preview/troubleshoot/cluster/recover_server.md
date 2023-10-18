---
title: Recover failed YB-TServer and YB-Master
linkTitle: Recover YB-TServer and YB-Master
description: Recover YB-TServer and YB-Master
aliases:
  - /troubleshoot/cluster/recover-server/
  - /preview/troubleshoot/cluster/recover-server/
menu:
  preview:
    parent: troubleshoot-cluster
    weight: 828
type: docs
---

A cluster might be running while a YB-TServer process, YB-Master process, or a node fails.

The following sample steps demonstrate how to recover a process if you have a N-node setup with replication factor (RF)=3.

## Monitor processes

It is recommended to have a cron or systemd setup to ensure that the YB-TServer and YB-Master processes are restarted if they are not running.

This handles transient failures, such as a node rebooting or process crash due to an unexpected behavior.

If you are using systemd and want to find the names of the services to restart, use the following command:

```sh
sudo systemctl list-units --type=service | grep yb
```

You should see output similar to the following:

```output
yb-controller.service                       loaded active running Yugabyte Controller
yb-master.service                           loaded active running Yugabyte master service
yb-tserver.service                          loaded active running Yugabyte tserver service
```

## Node failure

Typically, if a node has failed, the system automatically recovers and continues to function with the remaining N-1 nodes. If the failed node does not recover soon enough, and N-1 >= 3, then the under-replicated tablets will be re-replicated automatically to return to RF=3 on the remaining N-1 nodes.

If a node has experienced a permanent failure on a YB-TServer, you should start another YB-TServer process on a new node. This node will join the cluster, and the load balancer will automatically take the new YB-TServer into consideration and start rebalancing tablets to it.

## Master failure

If a new YB-Master needs to be started to replace a failed one, the master quorum needs to be updated.
Suppose, the original YB-Masters were n1, n2, n3. And n3 needs to be replaced with a new YB-Master n4. Then you need to use the `yb-admin` subcommand `change_master_config`, as follows:

```sh
./bin/yb-admin -master_addresses n1:7100,n2:7100 change_master_config REMOVE_SERVER n3 7100
./bin/yb-admin -master_addresses n1:7100,n2:7100 change_master_config ADD_SERVER n4 7100
```

The YB-TServer's in-memory state automatically learns of the new master after the `ADD_SERVER` step and does not need a restart.

You should update the configuration file of all YB-TServer processes which specifies the master addresses to reflect the new quorum of n1, n2, n4.

This is to handle the case if the `yb-tserver` restarts at some point in the future.

## Planned cluster changes

You might choose to perform planned cluster changes, such as moving the entire cluster to a brand new set of nodes (for example, move from machines of type A to type B). For instructions on how to do this, see [Change cluster configuration](../../../manage/change-cluster-config/).
