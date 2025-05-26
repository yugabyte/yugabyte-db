---
title: Handle node alerts
headerTitle: Handle node alerts
linkTitle: Node alerts
headcontent: What to do when you get a node alert
description: What to do when you get a node alert
menu:
  preview_yugabyte-platform:
    identifier: node-alerts
    parent: troubleshoot-yp
    weight: 15
type: docs
rightNav:
  hideH3: true
  hideH4: true
---

| Alert | Description | Action |
| :--- | :--- | :--- |
| DB Instance Down | A node is unreachable or down. | Check node status<br>Check provider<br>Check Prometheus |
| DB Node Restart | The OS rebooted. | SSH into host<br>Check OS logs<br>Investigate reboot cause |
| DB Instance Restart | A YugabyteDB process restarted<br>(not part of a universe update). | SSH into host<br>Check YugabyteDB and system logs |

## DB Instance Down

This alert fires when Prometheus is unable to scrape a node for metrics for (by default) more than 15 minutes.

#### What to do

1. Check the universe **Nodes** tab **Status** column.

1. If the status is Unreachable, confirm whether the host is up and reachable via your cloud provider (for example, AWS EC2) or on-premises environment.

1. If the host is down, restart it from the cloud console or equivalent.

    If necessary, follow the steps in [Replace a live or unreachable node](../../manage-deployments/remove-nodes/#replace-a-live-or-unreachable-node) to replace the node.

1. If the host is running, check the status of the node_exporter process. Prometheus uses the node_exporter service to export logs, and if the service is down, the node will appear unreachable.

    - Connect to the node and check the status of node_exporter by running the following command:

        ```sh
        ps -ef | grep node_exporter
        systemctl status node_exporter
        ```

    - Restart node_exporter if needed:

        ```sh
        sudo systemctl restart node_exporter
        ```

    - Confirm Prometheus access at `http://<node_ip>:9300`.

## DB Node Restart

This alert tracks OS-level restarts using the `node_boot_time` metric.

Note that the alert doesn't differentiate between TServer/Master process restarts, and full OS-level reboots.

#### What to do

1. SSH into the host and check if the reboot was planned or due to an issue.

1. Check logs as follows:

    ```sh
    cat /var/log/messages | grep -i reboot
    ```

    For Ubuntu:

    ```sh
    journalctl --list-boots
    ```

1. Check for other causes, such as power loss, kernel panic, or scheduled reboots.

## DB Instance Restart

This alert fires when Yugabyte process (TServer or Master) restarts without a planned update.

#### What to do

1. SSH into the node and inspect the following logs:

    - Yugabyte logs (`/home/yugabyte/tserver/logs/` or `/mnt/d0/yb-data/tserver/logs/`)
    - OS logs, for memory pressure or crash signals

1. Check whether a core dump or out of memory event occurred.

1. If a user or automation restarted the process, confirm the intent and whether alerts can be tuned.
