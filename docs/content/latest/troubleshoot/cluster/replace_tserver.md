---
title: Replace a failed YB-TServer
headerTitle: Replace a failed YB-TServer
linkTitle: Replace a failed YB-TServer
description: Procedure to replace a failed YB-TServer
aliases:
  - /troubleshoot/cluster/replace-tserver/
  - /latest/troubleshoot/cluster/replace-tserver/
menu:
  latest:
    identifier: replace-failed-tserver
    parent: troubleshoot-cluster
    weight: 829
isTocNested: true
showAsideToc: true
---

If you have a failed YB-TServer in a YugabytDB cluster that needs to be replaced, follow these steps:
scenario is:

## Start the new YB-TServer

Install and then start a new YB-TServer, making sure it is in the same placement group as the one you are replacing.

For details on starting YB-TServers and more options, see [Start YB-TServers](../../../../deploy/manual-deployment/start-tservers/).

## Blacklist the old yb-tserver

Blacklist the old yb-tserver: 
```bash
~/master/bin/yb-admin -master_addresses $MASTERS change_blacklist ADD $OLD_IP:9100
```

Refer to [change_blacklist](../../admin/yb-admin.md#change-blacklist) command for further parameters and options.

## Wait for the rebalance to complete

Wait for the data to drain from the failed YB-TServer and for the data to be loaded into the new one. You can check for the completion of rebalancing by running the following command:

```sh
~/master/bin/yb-admin -master_addresses $MASTERS get_load_move_completion 


{{< note title="Note" >}}

Loading and rebalancing will complete if only data from the failed YB-TSserver has someplace to be stored. Start the new YB-TServer first, or ensure your remaining YB-TServers have enough capacity and are in the correct placement zones.

{{< /note >}}

For details on using this command, see [`get_load_move_completion`](../../admin/yb-admin.md#get-load-move-completion).

## Kill old tserver
Once data move is at 100%, you can kill the old TS (eg: stop the TS process, terminate the VM)

Wait at least `tserver_unresponsive_timeout_ms` (default 60s) for this TS to be marked as dead by the master leader. 
You can validate this by visiting `$MASTER_LEADER_IP:7000/tablet-servers` and checking for the state of the old TS to say `DEAD`.

## Remove the failed YB-TServer from the blacklist 

Now that the replacement YB-TServer is up and running, and loaded with data, remove the address for the failed YB-TServer from the blacklist.

```sh
~/master/bin/yb-admin -master_addresses $MASTERS change_blacklist REMOVE node1:9100
