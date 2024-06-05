---
title: Apply operating system upgrades and patches to universe nodes
headerTitle: Patch and upgrade the Linux operating system
linkTitle: Patch Linux OS
description: Apply operating system upgrades and patches to universe nodes.
headcontent: Apply operating system upgrades and patches to universe nodes
menu:
  v2.20_yugabyte-platform:
    identifier: upgrade-nodes-2-onprem
    parent: manage-deployments
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../upgrade-nodes-csp/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>
      Public Cloud
    </a>
  </li>

  <li >
    <a href="../upgrade-nodes/" class="nav-link active">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

<!--  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>
-->
</ul>

If a virtual machine or a physical server in a universe requires operating system (OS) updates or patches, you need to pause node processes before applying updates.

Upgrades are performed via a rolling update, where one node in the universe is taken offline, patched, and restarted before updating the next. The universe continues to function normally during this process, however the upgrade can impact performance. For best results, do the following:

- Perform upgrades during low traffic periods to reduce the impact of a universe node being offline.
- Avoid performing upgrades during scheduled backups.

## Prerequisites

If your patching and upgrading process is likely to take longer than 15 minutes, increase the WAL log retention time. Set the WAL log retention time using the `--log_min_seconds_to_retain` YB-TServer flag. Refer to [Edit configuration flags](../edit-config-flags/).

Before you start, make sure that all nodes in the universe are running correctly.

## Patch nodes

Typically, the following sequence will be automated using scripts that call the YBA REST APIs.

For each node in the universe, use the following general procedure:

1. Stop the processes for the node to be patched.

    In YugabyteDB Anywhere (YBA), navigate to the universe **Nodes** tab, click the node **Actions**, and choose **Stop Processes**.

    If using the YBA API, use the following command:

    ```sh
    curl '<platform-url>/api/v1/customers/<customer_uuid>/universes/<universe_uuid>/nodes/<node_name>' -X 'PUT' -H 'X-AUTH-YW-API-TOKEN: <api-token>' -H 'Content-Type: application/json' -H 'Accept: application/json, text/plain, */*' \
    --data-raw '{"nodeAction":"STOP"}'
    ```

    Check the return status to confirm that the node is stopped.

1. Perform the steps to update or patch the Linux OS.

    Two common ways to patch or upgrade the OS of VMs include the following:

    - Inline patching - You modify the Linux OS binaries in place (for example, using yum).
    - Boot disk replacement - You create a separate new VM with a virtual disk containing the new Linux OS patch or upgrade, disconnect the virtual disk from the new VM, and use it to replace the DB node's boot disk. This is typically used with a hypervisor or public cloud.

        If the node uses assisted or fully manual provisioning, after replacing the boot disk, re-provision the node by following the [manual provisioning steps](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises-script/).

    Ensure that the node retains its IP addresses after the patching of the Linux OS. Also ensure that the existing data volumes on the node remain untouched by the OS patching mechanism.

1. Re-provision the node using the following API command:

    ```shell
    curl '<platform-url>/api/v1/customers/<customer_uuid>/universes/<universe_uuid>/nodes/<node_name>' -X 'PUT' -H 'X-AUTH-YW-API-TOKEN: <api-token>' -H 'Content-Type: application/json' -H 'Accept: application/json, text/plain, */*' \
    --data-raw '{"nodeAction":"REPROVISION"}'
    ```

1. Start the processes for the node.

    In YBA, navigate to the universe **Nodes** tab and, for the node, click **Actions** and choose **Start Processes**.

    If using the YBA API, use the following command:

    ```shell
    curl '<platform-url>/api/v1/customers/<customer_uuid>/universes/<universe_uuid>/nodes/<node_name>' -X 'PUT' -H 'X-AUTH-YW-API-TOKEN: <api-token>' -H 'Content-Type: application/json' -H 'Accept: application/json, text/plain, */*' \
    --data-raw '{"nodeAction":"START"}'
    ```

    Check the return status to confirm that the node is started.

When finished, you can confirm all nodes in the universe are running correctly in YBA by navigating to the universe **Nodes** tab.
