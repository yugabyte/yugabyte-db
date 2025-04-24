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

In particular, cron-based universes will no longer be supported in YugabyteDB Anywhere v2025.2 and later. Before you will be able to upgrade to v2025.2 or later, all your universes must be using systemd. YugabyteDB Anywhere will automatically upgrade universes that use a cloud provider configuration to systemd.

However, on-premises cron-based universes must be upgraded manually. To do this, in YugabyteDB Anywhere v2024.2.2 or later, navigate to the universe and choose **Actions>Upgrade to Systemd**.

## Node provisioning

[Legacy provisioning](../../prepare/server-nodes-software/software-on-prem-legacy/) workflows have been deprecated.

Provision nodes for on-premises universes using the `node-agent-provision.sh` script. Refer to [Automatically provision on-premises nodes](../../prepare/server-nodes-software/software-on-prem/).

YugabyteDB Anywhere v2024.2 and later require universes have node agent running on their nodes. Before you will be able to upgrade to v2024.2 or later, all your universes must be using node agent.

YugabyteDB Anywhere will attempt to automatically update universes. If it is unable to update a universe, make sure the universe nodes satisfy the [prerequisites](../../prepare/server-nodes-software/) and re-try the install by navigating to the universe and clicking **Actions>Install Node Agent**.

## xCluster

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/going-beyond-sql/asynchronous-replication-ysql/) for your universe was set up via yb-admin instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](../upgrade-yp-xcluster-ybadmin/).
