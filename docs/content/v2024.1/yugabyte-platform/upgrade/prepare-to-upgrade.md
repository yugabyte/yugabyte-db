---
title: Prepare to upgrade YugabyteDB Anywhere
headerTitle: Prepare to upgrade YugabyteDB Anywhere
linkTitle: Prepare to upgrade
description: Review changes that may affect installation
menu:
  v2024.1_yugabyte-platform:
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

YugabyteDB Anywhere v2024.2 requires Python v3.6 to v3.11. YugabyteDB Anywhere v2025.1 and later requires Python v3.10-3.11. If you are running YugabyteDB Anywhere on a system with an earlier Python, you will need to update Python on your system before you can upgrade YugabyteDB Anywhere to v2024.2 or later. (Note that this requirement applies only to the node running YugabyteDB Anywhere.)

In addition, both python and python3 must symbolically link to Python 3. Refer to [Prerequisites to deploy YBA on a VM](../../prepare/server-yba/).

## cron-based universes

cron-based universes are no longer supported in YugabyteDB Anywhere v2025.2 and later. Before you can upgrade to v2025.2 or later, all your universes must be using systemd.

To update cron-based universes, first upgrade YugabyteDB Anywhere to v2024.2.2 or later, and then refer to the steps in [Prepare to upgrade a universe](/v2024.2/yugabyte-platform/manage-deployments/upgrade-software-prepare/).

## Node provisioning

[Legacy on-premises node provisioning](../../prepare/server-nodes-software/software-on-prem-legacy/) workflows are deprecated in v2024.2 and later. Going forward, provision nodes for on-premises universes using the `node-agent-provision.sh` script. For more information, refer to [Automatically provision on-premises nodes](../../prepare/server-nodes-software/software-on-prem/).

{{< warning title="Legacy provisioning no longer available in v2025.2" >}}

v2025.2 (available late 2025) will not support legacy node provisioning. Before upgrading to 2025.2, be sure to update your node provisioning workflows to support automatic provisioning.

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

YugabyteDB Anywhere v2025.2 and later require universes have node agent running on their nodes. Before you can upgrade to v2025.2 or later, all your universes must be using node agent. (Note that this does not apply to universes deployed on Kubernetes.)

To upgrade a universe to node agent, first upgrade YugabyteDB Anywhere to v2024.2.5 or later, and then refer to the steps in [Prepare to upgrade a universe](/v2024.2/yugabyte-platform/manage-deployments/upgrade-software-prepare/).

## xCluster

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/going-beyond-sql/asynchronous-replication-ysql/) for your universe was set up via yb-admin instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](../upgrade-yp-xcluster-ybadmin/).
