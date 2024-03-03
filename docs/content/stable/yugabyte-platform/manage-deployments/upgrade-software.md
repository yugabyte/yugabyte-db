---
title: Upgrade universes with a new version of YugabyteDB
headerTitle: Upgrade the YugabyteDB software
linkTitle: Upgrade database
description: Use YugabyteDB Anywhere to upgrade the YugabyteDB software on universes.
headcontent: Perform rolling upgrades on live universe deployments
menu:
  stable_yugabyte-platform:
    identifier: upgrade-software
    parent: manage-deployments
    weight: 20
type: docs
---

The YugabyteDB release that is powering a universe can be upgraded to get new features and fixes included in the release.

When performing a database upgrade, do the following:

1. [Upgrade YugabyteDB Anywhere](../../upgrade/). You cannot upgrade a universe to a version of YugabyteDB that is later than the version of YugabyteDB Anywhere. To upgrade to a more recent version of YugabyteDB, you may first have to upgrade YugabyteDB Anywhere.

    For information on which versions of YugabyteDB are compatible with your version of YugabyteDB Anywhere, see [YugabyteDB Anywhere releases](../../../releases/yba-releases/).

1. [Review major changes in previous YugabyteDB releases](../upgrade-software-prepare/). Depending on the upgrade you are planning, you may need to make changes to your automation.

1. [View and import releases](../upgrade-software-install/#view-and-import-releases). Before you can upgrade your universe to a specific version of YugabyteDB, verify that the release is available and, if necessary, import the release into YugabyteDB Anywhere.

1. [Upgrade the universe](../upgrade-software-install/#upgrade-a-universe). Perform a rolling upgrade on a live universe deployment.

{{<index/block>}}

  {{<index/item
    title="Prepare to upgrade"
    body="Review changes that may affect your automation."
    href="../upgrade-software-prepare/"
    icon="/images/section_icons/quick_start/install.png">}}

  {{<index/item
    title="Upgrade a universe"
    body="Perform a rolling upgrade on a live universe deployment."
    href="../upgrade-software-install/"
    icon="/images/section_icons/quick_start/install.png">}}

{{</index/block>}}
