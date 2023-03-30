---
title: Replace a failed YB-TServer
headerTitle: Replace a failed YB-TServer
linkTitle: Replace a failed YB-TServer
description: Procedure to replace a failed YB-TServer
menu:
  preview:
    identifier: replace-failed-tserver
    parent: troubleshoot-cluster
    weight: 829
type: docs
---

You can replace a failed YB-TServer in a YugabyteDB cluster, as follows:

1. Install and then start a new YB-TServer, making sure it is in the same placement group as the one you are replacing. For detailed instructions, see [Start YB-TServers](../../../deploy/manual-deployment/start-tservers/).

2. Blacklist the failed YB-TServer by using the following command:

   ```sh
   ~/master/bin/yb-admin -master_addresses $MASTERS change_blacklist ADD $OLD_IP:9100
   ```

   For details, see [change_blacklist](../../../admin/yb-admin/#change-blacklist) in the yb-admin reference page.

3. Wait for the data to drain from the failed YB-TServer and for the data to be loaded into the new one. You can check for the completion of rebalancing by running the following command:

   ```sh
   ~/master/bin/yb-admin -master_addresses $MASTERS get_load_move_completion
   ```

   Loading and rebalancing will complete only if data from the failed YB-TServer has someplace to be stored. You would need to start the new YB-TServer first, or ensure your remaining YB-TServers have enough capacity and are in the correct placement zones.

   For details on using this command, see [`get_load_move_completion`](../../../admin/yb-admin/#get-load-move-completion).

4. When the data move is complete (100%), kill the failed YB-TServer by stopping the `yb-tserver` process or terminating the VM. Then wait for the YB-TServer to be marked as `DEAD` by the YB-Master leader. The YB-Master leader will mark the server as `DEAD` after not responding for one minute (based on the `tserver_unresponsive_timeout_ms`, with default being `60000`).

   To verify that the failed YB-TServer is dead, open your web browser to `$MASTER_LEADER_IP:7000/tablet-servers` and check the output.

5. Because the replacement YB-TServer is running and is loaded with data, remove the address for the failed YB-TServer from the blacklist, as follows:

   ```sh
   ~/master/bin/yb-admin -master_addresses $MASTERS change_blacklist REMOVE $OLD_IP:9100
   ```
