---
title: Handle node issues and alerts
headerTitle: Troubleshoot node issues
linkTitle: Node issues
headcontent: Node issues and alerts
description: What to do when you get a node alert
menu:
  v2025.2_yugabyte-platform:
    identifier: node-alerts
    parent: troubleshoot-yp
    weight: 15
type: docs
rightNav:
  hideH3: true
  hideH4: true
---

## Unable to register node agent

When [provisioning on-premises nodes](../../prepare/server-nodes-software/software-on-prem/) using the `node-agent-provision.sh` script, the following error displays:

```text
Unable to register node agent - Post "https://<YBA>/api/v1/customers/<uuid>/node_agents": context deadline exceeded (Client.Timeout exceeded while awaiting headers)
```

During provisioning, the node reaches YugabyteDB Anywhere over HTTPS (package download succeeds), but registration still fails. The root cause is often that YugabyteDB Anywhere cannot complete its registration-time callback to the node agent gRPC service on TCP 9070. The installer POST hangs until the HTTP client times out.

Ensure the node is accessible to YugabyteDB Anywhere over ports 9070 and 443. See [Networking requirements](../../prepare/networking/) for more information.

You can verify from the YugabyteDB Anywhere host using the following command during a retry:

```sh
nc -zv -w 10 <node-fqdn> 9070
```

If the command times out after 10 seconds with no response, the port is likely blocked by a firewall or security group; check that the ports are open. If you instead see "Connection refused," the port is reachable but the node agent service isn't running or listening on 9070.

## Handle alerts

Universes deployed using YugabyteDB Anywhere include following node [alerts](../../alerts-monitoring/alert/) by default:

- DB Instance Down. A node is unreachable or down.
- DB Node Restart. The operating system rebooted.
- DB Instance Restart. A YugabyteDB process restarted (outside of a universe update).

If you are notified of one of these alerts, you can take the following steps.

### DB Instance Down

This alert fires when Prometheus is unable to scrape a node for metrics for (by default) more than 15 minutes.

#### What to do

1. Check the universe **Nodes** tab **Status** column.

1. If the status is Unreachable, confirm whether the host is up and reachable via your cloud provider (for example, AWS EC2) or on-premises environment.

1. If the host is down, restart it from the cloud console or equivalent.

    If necessary, follow the steps in [Replace a live or unreachable node](../../manage-deployments/remove-nodes/#replace-a-live-or-unreachable-node) to replace the node.

1. If the host is running, check the status of the node_exporter process. Prometheus uses the node_exporter service to export metrics, and if the service is down, the node will appear unreachable.

    - Connect to the node and check the status of node_exporter by running the following command:

        ```sh
        ps -ef | grep node_exporter
        systemctl status node_exporter
        ```

    - Restart node_exporter if needed:

        ```sh
        sudo systemctl restart node_exporter
        ```

    - Confirm node_exporter access at `http://<node_ip>:9300`.

### DB Node Restart

This alert tracks OS-level restarts using the `node_boot_time` metric.

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

### DB Instance Restart

This alert fires when a YugabyteDB process (TServer or Master) restarts without a planned update.

#### What to do

1. SSH into the node and inspect the following logs:

    - YugabyteDB logs (`/home/yugabyte/tserver/logs/` or `/mnt/d0/yb-data/tserver/logs/`).
    - OS logs, for memory pressure or crash signals.

    Look for FATAL logs; the presence of a FATAL log file that corresponds with the time of the failure is a positive indicator for a crash and analyzing this log will likely point to the root cause.

1. Check whether a core dump or out of memory event occurred.

1. If a user or automation restarted the process, confirm the intent and whether alerts can be tuned.
