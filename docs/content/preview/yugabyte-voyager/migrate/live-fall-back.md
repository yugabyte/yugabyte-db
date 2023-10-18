---
title: Steps to perform live migration of your database using YugabyteDB Voyager
headerTitle: Live migration with fall-back
linkTitle: Live migration with fall-back
headcontent: Steps for a live migration with fall-back using YugabyteDB Voyager
description: Steps to ensure a successful live migration with fall-back using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: live-fall-back
    parent: migration-types
    weight: 104
techPreview: /preview/releases/versioning/#feature-availability
rightNav:
  hideH4: true
type: docs
---

When migrating using YugabyteDB Voyager, it is prudent to have a backup strategy if the new database doesn't work as expected. A fall-back approach involves streaming changes from the YugabyteDB (target) back to the source database post the cutover operation, enabling you to switchover back to the source database at any point.

A fall-back approach allows you to test the system end-to-end. This workflow is especially important in heterogeneous migration scenarios, in which source and target databases are using different engines.

## Fall-back workflow

![fall-back](/images/migrate/live-fall-back.png)

At cutover, applications stop writing to the source database and start writing to the target YugabyteDB database. After the cutover process is complete, YB Voyager keeps the source database synchronized with changes from the target Yugabyte DB as shown in the following illustration:

![cutover](/images/migrate/cutover.png)

Finally, if you need to switch back to the source database (because the current YugabyteDB system is not working as expected), you can switch back your database.

![fall-back-switchover](/images/migrate/fall-back-switchover.png)
