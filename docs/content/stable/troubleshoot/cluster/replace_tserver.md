---
title: Replace a failed YB-TServer
headerTitle: Replace a failed YB-TServer
linkTitle: Replace a failed YB-TServer
description: Procedure to replace a failed YB-TServer
menu:
  stable:
    identifier: replace-failed-tserver
    parent: troubleshoot-cluster
    weight: 829
type: docs
---

You can replace a failed YB-TServer in a YugabyteDB cluster, as follows:

1. Install and then start a new YB-TServer, making sure it is in the same placement group as the one you are replacing. For detailed instructions, see [Start YB-TServers](../../../deploy/manual-deployment/start-masters/#yb-tserver-servers/).

2. Blacklist the failed YB-TServer by using the following command:

   ```sh
   export MASTERS=yb-node1:7100,yb-node2:7100,yb-node3:7100
   ~/master/bin/yb-admin --master_addresses $MASTERS change_blacklist ADD $OLD_IP:9100
   ```

   For details, see [change_blacklist](../../../admin/yb-admin/#change-blacklist) in the yb-admin reference page.

3. Wait for the data to drain from the failed YB-TServer and for the data to be loaded into the new one. You can check for the completion of rebalancing by running the following command:

   ```sh
   ~/master/bin/yb-admin --master_addresses $MASTERS get_load_move_completion
   ```

   Loading and rebalancing will complete only if data from the failed YB-TServer has someplace to be stored. You would need to start the new YB-TServer first, or ensure your remaining YB-TServers have enough capacity and are in the correct placement zones.

   For details on using this command, see [`get_load_move_completion`](../../../admin/yb-admin/#get-load-move-completion).

4. When the data move is complete (100%), kill the failed YB-TServer by stopping the yb-tserver process or terminating the VM. Then wait for the YB-TServer to be marked as `DEAD` by the YB-Master leader. The YB-Master leader will mark the server as `DEAD` after not responding for one minute (based on the `tserver_unresponsive_timeout_ms`, with default being `60000`).

   To verify that the failed YB-TServer is dead, open your web browser to `$MASTER_LEADER_IP:7000/tablet-servers`
   or get the tablet server list using this command:
   
   ```sh
   ~/master/bin/yb-admin --master_addresses $MASTERS list_all_tablet_servers
   ```

5. In order to remove the failed server from the server list it needs to be dead and blacklisted.
   You made sure of that in the previous steps. The last condition is that it holds no more leases.
   Usually that should be the case at this point, so now the following command should remove the failed server:

   ```sh
   ~/master/bin/yb-admin --master_addresses $MASTERS remove_tablet_server <tserver-uuid>
   ```

   If you get an error message mentioning "xCluster-guarded information leases" you'll have to wait a little longer.
   Those leases take 2 minutes to expire (based on the `xcluster_guarded_lease_duration_ms`, with default being `120000`).

6. As a final cleanup step you should now remove the failed server's address from the backlist.
   If you skip this step and later re-install a YB-Tserver with the same address, that YB-Tserver will be blacklisted and will never be assigned any tablets.
   To remove that address from the blacklist run this command:

   ```sh
   ~/master/bin/yb-admin --master_addresses $MASTERS change_blacklist REMOVE $OLD_IP:9100
   ```
