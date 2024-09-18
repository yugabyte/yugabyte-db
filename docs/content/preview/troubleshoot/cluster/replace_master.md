---
title: Replace a failed YB-Master
headerTitle: Replace a failed YB-Master
linkTitle: Replace a failed YB-Master
description: Steps to replace a failed YB-Master in a YugabyteDB cluster.
aliases:
  - /troubleshoot/cluster/replace-master/
  - /preview/troubleshoot/cluster/replace-master/
menu:
  preview:
    identifier: replace-failed-master
    parent: troubleshoot-cluster
    weight: 831
type: docs
---

You can replace a failed YB-Master server in a YugabyteDB cluster.

Examples included in this document use the following scenario:

- The cluster includes three yb-master servers: `M1`, `M2`, `M3`.
- YB-Master server `M1` failed and needs to be replaced.
- A new YB-Master server (`M4`) will replace `M1`.
- The default master RPC port is `7100`

If the YB-Master to be replaced is already dead (for example, the VM was terminated), you would need to perform the `REMOVE` step first, and then the `ADD` step.

1. Start the replacement YB-Master server in standby mode by setting the `--master_addresses` flag to an empty string (`""`), as follows: 

   ```sh
   ./bin/yb-master --master_addresses="" --fs_data_dirs=<your_data_directories> [any other flags you would typically pass to this master process]
   ```

   When the `--master_addresses` is `""`, this YB-Master server starts without joining any existing master quorum. The node will be added to the master quorum in a later step.

2. Add the replacement YB-Master server into the existing cluster by running the [`yb-admin change_master_config ADD_SERVER`](../../../admin/yb-admin/#change-master-config) command, as follows:

   ```sh
   ./bin/yb-admin -master_addresses M1:7100,M2:7100,M3:7100 change_master_config ADD_SERVER M4 7100
   ```

3. Remove the failed YB-Master server from the cluster by using the [`yb-admin change_master_config REMOVE_SERVER`](../../../admin/yb-admin/#change-master-config) command, as follows:

   ```sh
   ./yb-admin -master_addresses M1:7100,M2:7100,M3:7100,M4:7100 change_master_config REMOVE_SERVER M1 7100
   ```

   Make sure to specify all YB-Master addresses, including M4, to make sure that if M4 becomes the leader, then yb-admin can find it.

4. Validate cluster by checking that your set of masters is now `M2`, `M3` and `M4`, as follows:

   ```bash
   ./yb-admin -master_addresses M2:7100,M3:7100,M4:7100 list_all_masters
   ```

Until [#1542](https://github.com/yugabyte/yugabyte-db/issues/1542) is implemented, the YB-TServer by default can only be aware of the YB-Master servers that are encoded in the `--tserver_master_addrs` flag with which they are started. If any of those YB-Master servers are still part of the active quorum, then they can propagate the new master quorum via heartbeats. If none of the current YB-Master servers are present in the YB-TServer flag, then the YB-TServer cannot join the cluster. Therefore, it is important to update `--tserver_master_addrs` on every YB-TServer to the new set of master addresses as `M2:7100,M3:7100,M4:7100`.