---
title: Steps to perform a successful migration to YugabyteDB.
headerTitle: Migrate
linkTitle: Migrate
image: /images/section_icons/index/quick_start.png
headcontent: Perform offline or live migration with YugabyteDB Voyager
description: Learn about offline, live, and live migration with fall-foward option to migrate your source database to your target YugabyteDB.
type: indexpage
menu:
  preview_yugabyte-voyager:
    identifier: migration-types
    parent: yugabytedb-voyager
    weight: 102
---

You can perform migration by taking your applications offline to perform the migration, migrate your data while your application is running (currently Oracle only), or add a fall-forward database for your live migration (currently Oracle only).

{{<index/block>}}

  {{<index/item
    title="Assess Migration"
    body="Generate a Migration Assessment Report to ensure a successful migration."
    href="assess-migration/"
    icon="fa-solid fa fa-file">}}

  {{<index/item
    title="Offline migration"
    body="Perform an offline migration of your database."
    href="migrate-steps/"
    icon="/images/section_icons/index/introduction.png">}}

  {{<index/item
    title="Live migration"
    body="Migrate your database while your application is running."
    href="live-migrate/"
    icon="/images/section_icons/manage/pitr.png">}}

  {{<index/item
    title="Live migration with fall-forward"
    body="Fall forward to a source-replica database for your live migration."
    href="live-fall-forward/"
    icon="/images/section_icons/manage/backup.png">}}

  {{<index/item
    title="Live migration with fall-back"
    body="Fall back to the source database for your live migration."
    href="live-fall-back/"
    icon="/images/section_icons/manage/backup.png">}}

  {{<index/item
    title="Bulk data load from files"
    body="Bulk load data from flat files stored locally or in cloud storage."
    href="bulk-data-load/"
    icon="/images/section_icons/manage/backup.png">}}

{{</index/block>}}
