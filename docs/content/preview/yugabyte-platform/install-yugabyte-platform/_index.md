---
title: Install
headerTitle: Install YugabyteDB Anywhere
linkTitle: Install
description: Install YugabyteDB Anywhere and prepare universe nodes.
image: fa-thin fa-wrench
headcontent: Install YugabyteDB Anywhere to begin creating and managing deployments
menu:
  preview_yugabyte-platform:
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

### Replicated

{{< warning title="Replicated end-of-life" >}}

{{</warning >}}

YugabyteDB Anywhere is ending support for Replicated at the end of 2024. For new installations of YugabyteDB Anywhere, use [YBA Installer](install-software/installer/).

To migrate existing Replicated YugabyteDB Anywhere installations to YBA Installer, refer to [Migrate from Replicated](migrate-replicated/).

{{<index/block>}}

  {{<index/item
    title="Migrate from Replicated"
    body="Migrate an installation from Replicated to YBA Installer."
    href="migrate-replicated/"
    icon="fa-thin fa-truck">}}

  {{<index/item
    title="Replicated [Deprecated]"
    body="Install YugabyteDB Anywhere software using Replicated."
    href="install-replicated/"
    icon="fa-thin fa-clone">}}

{{</index/block>}}
