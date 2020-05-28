---
title: Schedule a data backup
headerTitle: Schedule a data backup
linkTitle: Schedule a data backup
description: Use Yugabyte Platform to create a scheduled backup of a universe or cluster.
aliases:
  - /latest/manage/enterprise-edition/schedule-backups/
menu:
  latest:
    identifier: schedule-data-backup
    parent: enterprise-edition
    weight: 749
isTocNested: true
showAsideToc: true

---

Use Yugabyte Platform to perform a regularly scheduled backup of a full YugabyteDB universe or selected tables.

## Schedule a backup

1. In the YugabyteDB Admin Console, go to the universe that you want to schedule backups for.
2. Verify that backups are enabled by checking the **Tables** page. If disabled, click **Enable Backup** to enable backups.
3. Open the **Backups** tab.
4. Click **Create Backup**.
5. Use the **Tables to backup** dropdown to select tables or select **Full Universe Backup**.
6. Click **OK**.
7. Enter the desired frequency for backups. You can either enter the frequency in milliseconds (with the first backup being started immediately) or you can enter a `cron` expression.
8. Click **OK** to create the schedule.
9. To verify that the schedule has been created, click **Profile** and select **Schedules** from the drop-down.

## Disable a scheduled backup

If required, you can temporarily disable scheduled backups by going to the **Tables** tab and selecting **Disable Backups**.

## Delete a schedule

If you need to remove a scheduled backup, go to the **Schedules** page and for the schedule you want to delete, click **Options** and then select **Delete schedule**.