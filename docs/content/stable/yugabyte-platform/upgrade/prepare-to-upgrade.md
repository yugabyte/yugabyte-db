---
title: Prepare to upgrade YugabyteDB Anywhere
headerTitle: Prepare to upgrade YugabyteDB Anywhere
linkTitle: Prepare to upgrade
description: Review changes that may affect installation
menu:
  stable_yugabyte-platform:
    identifier: prepare-to-upgrade
    parent: upgrade
    weight: 50
type: docs
---

For information on which versions of YugabyteDB are compatible with your version of YugabyteDB Anywhere, see [YugabyteDB Anywhere releases](/stable/releases/yba-releases/).

For information on upgrading universes, refer to [Upgrade the YugabyteDB software](../../manage-deployments/upgrade-software/).

## High availability

If you are upgrading a YugabyteDB Anywhere installation with high availability enabled, follow the instructions provided in [Upgrade instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances).

## Operating system

If you are running YugabyteDB Anywhere on a [deprecated OS](../../../reference/configuration/operating-systems/), you need to update your OS before you can upgrade YugabyteDB Anywhere to the next major release.

## Python for YugabyteDB Anywhere

YugabyteDB Anywhere v2025.1 and later requires Python v3.10-3.11. If you are running YugabyteDB Anywhere on a system with Python earlier than 3.10, you will need to update Python on your system before you can upgrade YugabyteDB Anywhere to v2025.1 or later. (Note that this requirement applies only to the node running YugabyteDB Anywhere.)

In addition, both python and python3 must symbolically link to Python 3. Refer to [Prerequisites to deploy YBA on a VM](../../prepare/server-yba/).

## cron-based universes

cron and root-level systemd have been deprecated in favor of user-level systemd with node agent for management of universe nodes.

In particular, cron-based universes will no longer be supported in YugabyteDB Anywhere v2025.2 and later. Before you can upgrade to v2025.2 or later, all your universes must be using systemd. YugabyteDB Anywhere will automatically upgrade universes that use a cloud provider configuration to systemd.

However, on-premises cron-based universes must be upgraded manually. To do this, in YugabyteDB Anywhere v2024.2.2 or later, navigate to the universe and choose **Actions>Upgrade to Systemd**.

## Node provisioning

As of v2024.2, [legacy on-premises node provisioning](../../prepare/server-nodes-software/software-on-prem-legacy/) workflows have been deprecated. Going forward, provision nodes for on-premises universes using the `node-agent-provision.sh` script. For more information, refer to [Automatically provision on-premises nodes](../../prepare/server-nodes-software/software-on-prem/).

{{< warning title="Legacy provisioning no longer available in v2025.2" >}}

v2025.2 does not support legacy node provisioning. Before upgrading to 2025.2, be sure to update your node provisioning workflows to support automatic provisioning.

{{< /warning >}}

To upgrade a running on-premises universe to automatic provisioning, follow the [node patching](../../manage-deployments/upgrade-nodes/) procedure.

### Transparent hugepages

As of May 2025 (and affecting all customers on all versions), there is updated guidance for Transparent Hugepages (THP). THP should be enabled on nodes for optimal performance.

The required settings are described in [Transparent hugepages](../../prepare/server-nodes-software/#transparent-hugepages). Verify that all your DB nodes are configured in Linux with these settings.

Future versions of YugabyteDB Anywhere will flag universes with nodes that do not have these THP settings as mis-configured and/or unhealthy.

What action you take will depend on the type of provider used to create a universe, as described in the following table.

| Provider | Action |
| :--- | :--- |
| AWS, Google, Azure | Minimal user action needed.<br><br>For new universes, YBA automatically configures nodes with the correct THP settings.<br><br>For existing universes that lack THP or have THP mis-configured, YugabyteDB Anywhere will automatically configure THP as part any universe task that causes node re-provisioning. For example, upgrading Linux to apply security patches to nodes. |
| On-premises | Some user action is needed.<br><br>New nodes that you provision using [automatic provisioning](../../prepare/server-nodes-software/software-on-prem/) are automatically configured with the correct THP settings.<br><br>For existing nodes that lack THP or have THP mis-configured, THP settings are automatically configured during node re-provisioning if you follow the procedure for boot disk replacement as described in [Patch and upgrade the system](../../manage-deployments/upgrade-nodes/). You can do this when performing a regular Linux security patch (monthly, quarterly). |

## Node agent

YugabyteDB Anywhere v2025.2 and later require universes have node agent running on their nodes. Before you will be able to upgrade to v2025.2 or later, all your universes must be using node agent. (Note that this does not apply to universes deployed on Kubernetes.)

If any universe nodes require an update to node agent, YugabyteDB Anywhere displays a banner on the **Dashboard** to that effect.

You can manually update a universe to node agent by navigating to the universe and clicking **Actions>More>Install Node Agent**.

If you want YugabyteDB Anywhere to automatically update universes requiring node agent, on the banner, click **Automatically Install Node Agents**. YugabyteDB Anywhere will then attempt to update universe nodes to use node agent in the background. If it is unable to update a universe, click **View Node Agents** on the banner to display the **Node Agents** list, where you can identify problem nodes. Make sure the universe nodes satisfy the [prerequisites](../../prepare/server-nodes-software/) and re-try the install by clicking **Actions>Reinstall Node Agent** for the node in the **Node Agents** list.

<details> <summary>Additional settings</summary>

You can configure automatic node agent installation using the following [Runtime Configuration options](../../administer-yugabyte-platform/manage-runtime-config/).

- `yb.node_agent.enabler.run_installer`: Turn automatic node agent installation on or off. Global parameter.
- `yb.node_agent.enabler.reinstall_cooldown`: If installation fails on a node, YugabyteDB Anywhere tries again after this period expires (default is 24 hours).
- `yb.node_agent.client.enabled`: Set to false for a provider to prevent automatic installation. Provider parameter.

Note that only a Super Admin user can modify Global configuration settings.

</details>

## xCluster

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/going-beyond-sql/asynchronous-replication-ysql/) for your universe was set up via yb-admin instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](../upgrade-yp-xcluster-ybadmin/).
