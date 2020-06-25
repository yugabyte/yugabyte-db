---
title: Replace a failed YB-Master
headerTitle: Replace a failed YB-Master
linkTitle: Replace a failed YB-Master
description: Steps to replace a failed YB-Master in a YugabyteDB cluster.
aliases:
  - /troubleshoot/cluster/replace-master/
  - /latest/troubleshoot/cluster/replace-master/
menu:
  latest:
    identifier: replace-failed-master
    parent: troubleshoot-cluster
    weight: 831
isTocNested: true
showAsideToc: true
---

To replace a failed YB-Master server in a YugabyteDB cluster, follow these steps:

For the steps below, the examples use the following scenario:

- The cluster includes three `yb-master` servers: `M1`, `M2`, `M3`.
- YB-Master server `M1` failed and needs to be replaced.
- A new YB-Master server (`M4`) will replace `M1`.
- The default master RPC port is `7100`



## Start new master and add to cluster
Start `M4` with `--master_addresses= (empty string)`, this will put the master in what we refer to as `ShellMode`.

Add the new master into the quorum using:
```bash
./yb-admin -master_addresses M1:7100,M2:7100,M3:7100 change_master_config ADD_SERVER M4 7100
```

## Remove old master
Remove the old master from the quorum using:
```bash
./yb-admin -master_addresses M1:7100,M2:7100,M3:7100,M4:7100 change_master_config REMOVE_SERVER M1 7100
```

Note: the master addresses needs to include all 4 masters, in case the new one is suddenly the master leader!)

## Validate cluster

Validate that your set of masters is now `M2`, `M3` and `M4` using:
```bash
yb-admin -master_addresses M2:7100,M3:7100,M4:7100 list_all_masters
```

Until [#1542](https://github.com/yugabyte/yugabyte-db/issues/1542) is implemented, the TS will by default only know of 
whatever masters are encoded in the `--tserver_master_addrs` flag that they are started with. 

If any one of those masters is still part of the active quorum, then they will propagate the new master quorum over via heartbeats. 
If, however, none of the current masters are present in the TS flag, then the TS will not be able to join the cluster! 

So it is important to make sure to update `--tserver_master_addrs` on every TS to the new set of master addresses, `M2:7100,M3:7100,M4:7100`!

If the master you wish to replace is already dead (eg: VM was terminated), you might want to first do the `REMOVE` step, then do the `ADD` step.
