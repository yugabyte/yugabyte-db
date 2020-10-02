---
title: Restore universe data
headerTitle: Restore universe data from a backup
linkTitle: Restore data
description: Use Yugabyte Platform to restore data in YCQL tables.
menu:
  stable:
    parent: back-up-restore-universes
    identifier: restore-universe-data
    weight: 30
isTocNested: true
showAsideToc: true
---

On that same completed task, click on the **Actions** dropdown and click **Restore Backup**.
You will see a modal where you can select the universe, keyspace, and table you want to restore to. Enter in
values like this (making sure to change the table name you restore to) and click **OK**.

![Restore Modal](/images/ee/restore-backup-modal.png)

If you now go to the **Tasks** tab, you will eventually see a completed **Restore Backup** task. To
confirm this worked, go to the **Tables** tab to see both the original table and the table you
restored to.

![Tables View](/images/ee/tables-view.png)