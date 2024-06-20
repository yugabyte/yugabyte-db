---
title: Upgrade universes with a new version of YugabyteDB
headerTitle: Upgrade the YugabyteDB software
linkTitle: Upgrade database
description: Use YugabyteDB Anywhere to upgrade the YugabyteDB software on universes.
headcontent: Perform rolling upgrades on live universe deployments
menu:
  preview_yugabyte-platform:
    identifier: upgrade-software
    parent: manage-deployments
    weight: 20
type: docs
---

The YugabyteDB release that is powering a universe can be upgraded to get new features and fixes included in the release.

{{< note title="Upgrading YugabyteDB on deprecated operating systems" >}}

If your universe is running on a [deprecated OS](../../../reference/configuration/operating-systems/), you will need to update your OS before you can upgrade to the next major YugabyteDB release. Refer to [Patch and upgrade the Linux operating system](../upgrade-nodes/).

{{< /note >}}

When performing a database upgrade, do the following:

1. [Upgrade YugabyteDB Anywhere](../../upgrade/). You cannot upgrade a universe to a version of YugabyteDB that is later than the version of YugabyteDB Anywhere.

    For information on which versions of YugabyteDB are compatible with your version of YugabyteDB Anywhere, refer to [Compatibility with YugabyteDB](../../../releases/yba-releases/#compatibility-with-yugabytedb).

1. [Prepare to upgrade a universe](../upgrade-software-prepare/). Depending on the upgrade you are planning, you may need to make changes to your automation or upgrade your Linux operating system.

1. [View and import releases](../ybdb-releases/). Before you can upgrade your universe to a specific version of YugabyteDB, verify that the release is available in YugabyteDB Anywhere and, if necessary, import the release.

1. [Upgrade the universe](../upgrade-software-install/). Perform a rolling upgrade on a live universe deployment.

{{<index/block>}}

  {{<index/item
    title="Prepare to upgrade"
    body="Review changes that may affect your automation."
    href="../upgrade-software-prepare/"
    icon="/images/section_icons/quick_start/install.png">}}

  {{<index/item
    title="Manage releases"
    body="View and import the latest releases of YugabyteDB."
    href="../ybdb-releases/"
    icon="/images/section_icons/quick_start/install.png">}}

  {{<index/item
    title="Upgrade a universe"
    body="Perform a rolling upgrade on a live universe deployment."
    href="../upgrade-software-install/"
    icon="/images/section_icons/quick_start/install.png">}}

{{</index/block>}}
