---
title: Upgrade YugabyteDB Anywhere
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade
description: Upgrade YugabyteDB Anywhere.
headcontent: Keep YugabyteDB Anywhere up to date
menu:
  v2025.2_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: upgrade
    weight: 700
type: indexpage
---

Upgrade to the latest version of YugabyteDB Anywhere for the latest fixes and improvements, and to be able to [upgrade your universes](../manage-deployments/upgrade-software/) to the latest version of YugabyteDB.

You should run the latest version of YugabyteDB Anywhere that is compatible with the versions of YugabyteDB that are being used by your universes. You cannot upgrade a universe to a version of YugabyteDB that is later than the version of YugabyteDB Anywhere. For information on which versions of YugabyteDB are compatible with your version of YugabyteDB Anywhere, see [Compatibility with YugabyteDB](../../releases/yba-releases/#compatibility-with-yugabytedb).

For information on upgrading universes, see [Upgrade the YugabyteDB software](../manage-deployments/upgrade-software/).

{{<index/block>}}

  {{<index/item
    title="Prepare for your upgrade"
    body="Review changes that may affect your upgrade."
    href="prepare-to-upgrade/"
    icon="fa-thin fa-diamond-exclamation">}}

  {{<index/item
    title="Upgrade YugabyteDB Anywhere"
    body="Upgrade your YugabyteDB Anywhere installation."
    href="upgrade-yp-installer/"
    icon="fa-thin fa-up-from-bracket">}}

{{</index/block>}}
