---
title: Prepare to upgrade universes with a new version of YugabyteDB
headerTitle: Prepare to upgrade a universe
linkTitle: Prepare to upgrade
description: Review changes that may affect your automation when upgrading universes.
headcontent: Review changes that may affect your automation
menu:
  stable_yugabyte-platform:
    identifier: upgrade-software-prepare
    parent: upgrade-software
    weight: 10
type: docs
---

## Verify software requirements for nodes

Make sure the universe nodes meet the software requirements for running the version of YugabyteDB you are installing.

Refer to [Software requirements for database nodes](../../prepare/server-nodes-software/).

## Upgrade the operating system

If your universe is running on a [deprecated OS](../../../reference/configuration/operating-systems/), you need to update your OS before you can upgrade to the next major release of YugabyteDB. Refer to [Patch and upgrade the Linux operating system](../upgrade-nodes/).

## cron-based universes

cron and root-level systemd have been deprecated in favor of user-level systemd with node agent for management of universe nodes.

In particular, cron-based universes are no longer supported in YugabyteDB Anywhere v2025.2 and later. Before you will be able to upgrade to v2025.2 or later, all your universes must be using systemd. YugabyteDB Anywhere will automatically upgrade universes that use a cloud provider configuration to systemd.

However, on-premises cron-based universes must be upgraded manually. To do this, in YugabyteDB Anywhere v2024.2.2 or later, navigate to the universe and choose **Actions>Upgrade to Systemd**.

## Node agent

YugabyteDB Anywhere v2025.2 and later require universes have node agent running on their nodes. Before you will be able to upgrade to v2025.2 or later, all your universes must be using node agent. (Note that this does not apply to universes deployed on Kubernetes.)

To upgrade a universe to node agent, first make sure the universe is not cron-based and if necessary [update the universe to systemd](#cron-based-universes). Then navigate to the universe and click **Actions>More>Install Node Agent**. If installation fails on a node, make sure the node satisfies the [prerequisites](../../prepare/server-nodes-software/) and re-try the install.

You can configure YugabyteDB Anywhere to automatically update universes to node agent in the background. Refer to [Prepare to upgrade YugabyteDB Anywhere](../../upgrade/prepare-to-upgrade/#node-agent).

## Transparent hugepages

Transparent hugepages (THP) should be enabled on nodes for optimal performance. If you have on-premises universes with legacy provisioning where THP are not enabled, you can update THP settings by following the [node patching](../../manage-deployments/upgrade-nodes/) procedure; THP settings are automatically updated in step 3 when re-provisioning the node.

## Backups and point-in-time-recovery

- Backups

  - Backups taken on a newer version cannot be restored to universes running a previous version.
  - Backups taken during the upgrade cannot be restored to universes running a previous version.
  - Backups taken before the upgrade _can_ be used for restore to the new version.

- [Point-in-time-recovery](../../back-up-restore-universes/pitr/) (PITR)

  When you start the [upgrade](../upgrade-software-install/#perform-the-upgrade), the PITR change history is invalidated. This means that after an upgrade starts, you will no longer be able to access or restore to any time before the upgrade was started - _regardless of the outcome of the upgrade_.

  During the [monitoring phase](../upgrade-software-install/#monitor-the-universe) (that is, after upgrading but before finalizing or rolling back), any attempt to perform any PITR-based actions (such as rewind or clone a database to a point in time, back up and restore a database with PITR, or inspect a database at a point in time) will fail.

  After [finalizing](../upgrade-software-install/#finalize-an-upgrade) or [rolling back](../upgrade-software-install/#roll-back-an-upgrade) the upgrade, PITR-based actions become available again. However, keep in mind the following:

  - After finalizing, you cannot perform a PITR-based action targeting a time before the upgrade was started.
  - After rollback, you cannot perform a PITR-based action targeting a time before the upgrade was started.

## Review major changes in previous YugabyteDB releases

{{< warning title="For YugabyteDB upgrades in YugabyteDB Anywhere" >}}
You can only upgrade from a stable version to another stable version, or from a preview version to another preview version. Optionally, you can set a runtime flag `yb.skip_version_checks`, to skip all YugabyteDB version checks during upgrades. For more information, contact {{% support-platform %}}.
{{< /warning >}}

Before starting the upgrade, review the following major changes in previous YugabyteDB releases. Depending on the upgrade you are planning, you may need to make changes to your automation.

### Upgrading from versions earlier than v2.16.0

The YB Controller (YBC) service was introduced in v2.16.0 for all universes (except Kubernetes), and is required for YugabyteDB Anywhere v2.16.0 and later.

YBC is used to manage backup and restore, providing faster full backups, and introduces support for incremental backups.

**Impacts**

- Firewall ports - update your firewall rules to allow incoming TCP traffic on port 18018, which is used by YBC, for all nodes in a universe.

- On-premises provider - if you use on-premises providers with manually-provisioned nodes, update your current procedures for manually provisioning instances to accommodate YBC. This includes the following:

  - Set systemd-specific database service unit files (if used). Refer to [Manually provision on-premises nodes](../../prepare/server-nodes-software/software-on-prem-manual/).

  - After upgrading nodes, manually install YBC on the nodes. Refer to [Upgrade manually-provisioned on-premises universe](../upgrade-software-install/#upgrade-manually-provisioned-on-premises-universe).

- OS patching procedure - for universes created using an on-premises provider with manually-provisioned nodes, if your OS patching procedures involve re-installing YugabyteDB software on a node, you will need to update those procedures to accommodate YBC.

### Upgrading from versions earlier than v2.18.0

YBC was introduced for Kubernetes clusters in v2.18.0. Refer to [Upgrading from versions earlier than v2.16.0](#upgrading-from-versions-earlier-than-v2-16-0).

### Upgrading from versions earlier than v2.18.2

The YugabyteDB Anywhere Node Agent was introduced for all universes in v2.18.2. Node agent is an RPC service running on a YugabyteDB node, and is used to manage communication between YugabyteDB Anywhere and the nodes in universes. Except for Day 0 tasks during initial installation, YugabyteDB Anywhere no longer uses SSH and SCP to manage nodes; instead, YugabyteDB Anywhere connects to the Node agent process listening on port 9070, and performs all its management via this secure connection. For more information, refer to the [Node agent FAQ](/stable/faq/yugabyte-platform/#node-agent).

**Impacts**

- Firewall ports - update your firewall rules to allow incoming TCP traffic on port 9070 for all nodes in a universe. YugabyteDB Anywhere listens to node agents on port 443.

- On-premises provider - if you use on-premises providers with manually-provisioned nodes, you will need to update your current procedures for manually provisioning instances to include installing node agent. Refer to [Install node agent](../../prepare/server-nodes-software/software-on-prem-manual/#install-node-agent).

- [OS patching](../../manage-deployments/upgrade-nodes/) procedure - for universes created using an on-premises provider with manually-provisioned nodes, if your OS patching procedures involve re-installing YugabyteDB software on a node, you will need to update those procedures to accommodate node agent.
