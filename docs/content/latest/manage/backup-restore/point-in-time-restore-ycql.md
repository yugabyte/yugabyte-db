---
title: Point-in-Time Restore for YCQL
headerTitle: Point-in-time restore
linkTitle: Point-in-time restore
description: Restore data from a specific point in time in YugabyteDB for YCQL
beta: /latest/faq/general/#what-is-the-definition-of-the-beta-feature-tag
aliases:
menu:
  latest:
    identifier: point-in-time-restore-2-ycql
    parent: backup-restore
    weight: 704
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/manage/backup-restore/point-in-time-restore-ycql" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li >
    <a href="/latest/manage/backup-restore/point-in-time-restore-ysql" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>

{{< note>}}
Refer to [Recovery scenarios](../point-in-time-restore-ysql#recovery-scenarios), [Features](../point-in-time-restore-ysql#features), [Use cases](../point-in-time-restore-ysql#use-cases), and [Limitations](#limitations) for details on this feature.
{{</ note >}}

## Try out the PITR feature

There are [several recovery scenarios](../../../explore/backup-restore/point-in-time-restore-ycql/) in the Explore section.

## Limitations

This is a BETA feature, and is in active development. Currently, you can recover from the following YCQL operations:

* Data changes
* CREATE and DROP TABLE
* ALTER TABLE (including ADD and DROP COLUMN)
* CREATE and DROP INDEX

Development for this feature is tracked in [issue 7120](https://github.com/yugabyte/yugabyte-db/issues/7120). Some forthcoming features include:

* Recovery from a TRUNCATE TABLE
* Roles and permissions
