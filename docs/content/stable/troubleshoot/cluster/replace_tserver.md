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

1. Blacklist the failed YB-TServer by using the following command:

   ```sh
   ~/master/bin/yb-admin --master_addresses <master-addresses> change_blacklist ADD <failed-tserver-ip>:9100
   ```

   `<master-addresses>` is a comma-separated list of YB-Master hosts and ports, such as `yb-node1:7100,yb-node2:7100,yb-node3:7100`.

   For details, see [change_blacklist](../../../admin/yb-admin/#change-blacklist) in the yb-admin reference page.

1. Wait for the data to drain from the failed YB-TServer and for the data to be loaded into the new one. You can check for the completion of rebalancing by running the following command:

   ```sh
   ~/master/bin/yb-admin --master_addresses <master-addresses> get_load_move_completion
   ```

   Loading and rebalancing will complete only if data from the failed YB-TServer has someplace to be stored. You would need to start the new YB-TServer first, or ensure your remaining YB-TServers have enough capacity and are in the correct placement zones.

   For details on using this command, see [`get_load_move_completion`](../../../admin/yb-admin/#get-load-move-completion).

1. When the data move is complete (100%), kill the failed YB-TServer by stopping the yb-tserver process or terminating the VM. Then wait for the YB-TServer to be marked as `DEAD` by the YB-Master leader. The YB-Master leader will mark the server as `DEAD` after not responding for one minute (based on the `tserver_unresponsive_timeout_ms`, with default being `60000`).

   To verify that the failed YB-TServer is dead, open your web browser to `<master-leader-ip>:7000/tablet-servers`, or get the tablet server list using this command:

   ```sh
   ~/master/bin/yb-admin --master_addresses <master-addresses> list_all_tablet_servers
   ```

   Note the UUID of the failed YB-TServer (the `Tablet Server UUID` for `<failed-tserver-ip>:9100`). You need it in the next step.

1. Remove the failed server from the server list. The server must be dead, still blacklisted, and hosting no tablets. Run the following command:

   ```sh
   ~/master/bin/yb-admin --master_addresses <master-addresses> remove_tablet_server <tserver-uuid>
   ```

   <!-- TODO Restore after the 2026.2 release. `xcluster_guarded_lease_duration_ms` is not in the current stable release (2026.1).
   If this command returns `Cannot remove tablet server <tserver-uuid> because it may still hold a xCluster-guarded information lease`, wait and retry. The lease expires about `xcluster_guarded_lease_duration_ms` (default `120000`, 2 minutes) plus clock-skew slack after the failed server's last heartbeat. The server is marked `DEAD` about one minute after that heartbeat (`tserver_unresponsive_timeout_ms`, default `60000`), so about another minute usually remains.
   -->

1. As a final cleanup step, remove the failed server's address from the blacklist. Do this after `remove_tablet_server`: the server must still be blacklisted, and removing the address first makes that command fail.

   If you skip this step and later re-install a YB-TServer with the same address, that YB-TServer will be blacklisted and will never be assigned any tablets.

   To remove the address from the blacklist, run this command:

   ```sh
   ~/master/bin/yb-admin --master_addresses <master-addresses> change_blacklist REMOVE <failed-tserver-ip>:9100
   ```
