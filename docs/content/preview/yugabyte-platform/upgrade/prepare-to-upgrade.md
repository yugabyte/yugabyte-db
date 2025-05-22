---
title: Prepare to upgrade YugabyteDB Anywhere
headerTitle: Prepare to upgrade YugabyteDB Anywhere
linkTitle: Prepare to upgrade
description: Review changes that may affect installation
menu:
  preview_yugabyte-platform:
    identifier: prepare-to-upgrade
    parent: upgrade
    weight: 50
type: docs
---

For information on which versions of YugabyteDB are compatible with your version of YugabyteDB Anywhere, see [YugabyteDB Anywhere releases](/preview/releases/yba-releases/).

## High availability

If you are upgrading a YugabyteDB Anywhere installation with high availability enabled, follow the instructions provided in [Upgrade instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances).

## Operating system

If you are running YugabyteDB Anywhere on a [deprecated OS](../../../reference/configuration/operating-systems/), you need to update your OS before you can upgrade YugabyteDB Anywhere to the next major release.

## Python

YugabyteDB Anywhere v25.1 and later requires Python v3.10-3.12. If you are running YugabyteDB Anywhere on a system with Python earlier than 3.10, you will need to update Python on your system before you can upgrade YugabyteDB Anywhere to v25.1 or later.

## cron-based universes

cron and root-level systemd have been deprecated in favor of user-level systemd with node agent for management of universe nodes.

In particular, cron-based universes will no longer be supported in YugabyteDB Anywhere v2025.2 (LTS release planned for end of 2025) and later. Before you will be able to upgrade to v2025.2 or later, all your universes must be using systemd. YugabyteDB Anywhere will automatically upgrade universes that use a cloud provider configuration to systemd.

However, on-premises cron-based universes must be upgraded manually. To do this, in YugabyteDB Anywhere v2024.2.2 or later, navigate to the universe and choose **Actions>Upgrade to Systemd**.

## Node provisioning

As of v2024.2, [legacy node provisioning](../../prepare/server-nodes-software/software-on-prem-legacy/) workflows have been deprecated. Going forward, provision nodes for on-premises universes using the `node-agent-provision.sh` script. For more information, refer to [Automatically provision on-premises nodes](../../prepare/server-nodes-software/software-on-prem/).

To upgrade a running on-premises universe to automatic provisioning, follow the [node patching](../../manage-deployments/upgrade-nodes/) procedure.

### Transparent hugepages

As of May 2025 (and affecting all customers on all versions), there is updated guidance for Transparent Hugepages (THP). THP should be enabled on nodes for optimal performance.

The required settings are described in [Transparent hugepages](../../prepare/server-nodes-software/#transparent-hugepages). Verify that all your DB nodes are configured in Linux with these settings.

Future versions of YugabyteDB Anywhere will flag universes with nodes that do not have these THP settings as mis-configured and/or unhealthy.

What action you take will depend on the type of provider used to create a universe, as described in the following table.

| Provider | Action |
| :--- | :--- |
| AWS, Google, Azure | No user action is needed.<br><br>For new universes, YBA automatically configures nodes with the correct THP settings.<br><br>For existing universes that lack THP or have THP mis-configured, YugabyteDB Anywhere will automatically configure THP as part any universe task that causes node re-provisioning. For example, upgrading Linux to apply security patches to nodes. |
| On-premises | Some user action is needed.<br><br>New nodes that you provision using [automatic provisioning](../../prepare/server-nodes-software/software-on-prem/) are automatically configured with the correct THP settings.<br><br>For existing nodes that lack THP or have THP mis-configured, THP settings are automatically configured during node re-provisioning if you follow the procedure for boot disk replacement as described in [Patch and upgrade the system](../../manage-deployments/upgrade-nodes/). You can do this when performing a regular Linux security patch (monthly, quarterly). |

## Node agent

YugabyteDB Anywhere v2025.2 (LTS release planned for end of 2025) and later require universes have node agent running on their nodes. Before you will be able to upgrade to v2025.2 or later, all your universes must be using node agent.

To upgrade a universe to node agent, first make sure the universe is not cron-based and if necessary [update the universe to systemd](#cron-based-universes). Then navigate to the universe and click **Actions>Install Node Agent**. If installation fails on a node, make sure the node satisfies the [prerequisites](../../prepare/server-nodes-software/) and re-try the install.

## xCluster

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/going-beyond-sql/asynchronous-replication-ysql/) for your universe was set up via yb-admin instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](../upgrade-yp-xcluster-ybadmin/).
