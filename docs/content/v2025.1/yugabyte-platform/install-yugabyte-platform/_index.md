---
title: Install
headerTitle: Install YugabyteDB Anywhere
linkTitle: Install
description: Install YugabyteDB Anywhere and prepare universe nodes.
headcontent: Install YugabyteDB Anywhere to begin creating and managing deployments
menu:
  v2025.1_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: install-yugabyte-platform
weight: 610
type: indexpage
---

For on-premises and public cloud deployment, you install YugabyteDB Anywhere using YBA Installer. YBA Installer is a standalone binary that you can use to perform online or airgapped installations, as well as manage and upgrade existing installations, and migrate Replicated installations.

Kubernetes installations are performed and managed using the YugabyteDB Anywhere Helm chart.

{{<index/block>}}

  {{<index/item
    title="Install YugabyteDB Anywhere"
    body="Install YugabyteDB Anywhere software on a host using YBA Installer."
    href="install-software/installer/"
    icon="fa-thin fa-wrench">}}

  {{<index/item
    title="Create Admin user"
    body="Create your YugabyteDB Anywhere Super Admin user."
    href="create-admin-user/"
    icon="fa-thin fa-user-crown">}}

{{</index/block>}}

### Migrate from Replicated

YugabyteDB Anywhere has ended support for installing using Replicated. For new installations of YugabyteDB Anywhere, use [YBA Installer](install-software/installer/).

To migrate existing Replicated YugabyteDB Anywhere installations to YBA Installer:

1. If your YugabyteDB Anywhere Replicated installation is v2.18.5 or earlier, or v2.20.0, use Replicated to [upgrade your installation](../upgrade/upgrade-yp-replicated/) to v2.20.1.3 or later.

1. [Migrate from Replicated](migrate-replicated/) using YBA Installer.

{{<index/block>}}

  {{<index/item
    title="Migrate from Replicated"
    body="Migrate an installation from Replicated to YBA Installer."
    href="migrate-replicated/"
    icon="fa-thin fa-truck">}}

{{</index/block>}}
