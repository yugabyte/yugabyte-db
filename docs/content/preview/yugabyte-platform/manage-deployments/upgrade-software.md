---
title: Upgrade the YugabyteDB software
headerTitle: Upgrade the YugabyteDB software
linkTitle: Upgrade YugabyteDB software
description: Use YugabyteDB Anywhere to upgrade the YugabyteDB software.
menu:
  latest:
    identifier: upgrade-software
    parent: manage-deployments
    weight: 80
isTocNested: true
showAsideToc: true
---

The YugabyteDB release that is powering a universe can be upgraded to get the new features and fixes included in the release.

You can perform a rolling upgrade on a live universe deployment as follows:

1. Navigate to **Universes** and select your universe.
2. Click **Actions > Upgrade Software**.
3. In the **Upgrade Software** dialog, ensure that **Rolling Upgrade** is enabled, define the delay between servers or accept the default value, and then use the **Server Version** field to select the new YugabyteDB version, as per the following illustration:<br><br>

   ![Upgrade Universe Confirmation](/images/ee/upgrade-univ-2.png)

