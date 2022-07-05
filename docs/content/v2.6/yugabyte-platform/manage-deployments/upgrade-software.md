---
title: Upgrade the YugabyteDB software
headerTitle: Upgrade the YugabyteDB software
linkTitle: Upgrade the YugabyteDB software
description: Use Yugabyte Platform to upgrade the YugabyteDB software.
menu:
  v2.6_yugabyte-platform:
    identifier: upgrade-software
    parent: manage-deployments
    weight: 80
type: docs
---

The YugabyteDB release that is powering a universe can be upgraded to obtain new features and bug fixes.

A rolling upgrade can be performed on a live universe deployment by following these steps:

1. Open your universe.

2. Click **More > Upgrade Software**, as per the following illustration:<br><br>

   ![Upgrade Universe Dropdown](/images/ee/upgrade-univ-1.png)<br><br>

3. In the confirmation dialog, select the new YugabyteDB version to start the upgrade of the universe in a rolling manner, as per the following illustration:<br><br>

   ![Upgrade Universe Confirmation](/images/ee/upgrade-univ-2.png)<br><br><br>

{{< note title="Note" >}}

If you have upgraded the YugabyteDB software to version 2.8.0.0 or later, you need to manually run the `upgrade_sql` command on your universes.

{{< /note >}}
