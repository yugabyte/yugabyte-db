---
title: Back up data
linkTitle: Back up data
description: Back up data
image: /images/section_icons/manage/enterprise.png
headcontent: Back up data in YugabyteDB.
aliases:
  - /manage/backup-restore/backing-up-data
menu:
  latest:
    identifier: back-up-data
    parent: manage-backup-restore
    weight: 703
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/manage/backup-restore/back-up-data" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="/latest/manage/backup-restore/back-up-data-ycql" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

YugabyteDB is designed for reliability, providing high fault tolerance and redundancy. Many YSQL applications do not require backups, except for disaster recovery needs or to create YugabyteDB clusters for development and testing. When you need backups, YugabyteDB provides two utilities, `ysql_dump` and `ysql_dumpall`, to back up databases.

{{< note title="Note" >}}

Enhancements to provide documentation and a command line tool for distributed backup and restore functionality can be tracked in [GitHub issue #2350](https://github.com/yugabyte/yugabyte-db/issues/2350).

{{< /note >}}

## Back up a database

The YugabyteDB[ `ysql_dump`](../../../admin/ysql-dump) backup utility, derived from PostgreSQL `pg_dump`, 
can be used to extract a single YugabyteDB database into a SQL script file.  `ysql_dump` will make a consistent backup for a database, even if it is being used concurrently, and does not block others database users (readers or writers).

The SQL script dump is a plain-text file that include the SQL statements required to restore the database to the state it was in at the time it was saved. To restore a database from the SQL script file, use [`ysqlsh`](../../../admin/ysqlsh). For details on restoring an individual YugabyteDB database, see [Restore data](../restore-data).

To back up all of the databases in a YugabyteDB cluster, including metadata (roles and other database objects), use the `ysql_dumpall` utility.

To back up one database, follow these steps:

1. Open a command line window and change to your YugabyteDB home directory.

2. Run the `ysql_dumpall` command.

```sh
$ ./postgres/bin/ysql_dumpall -U yugabyte > ../backup/yb-all.sql
```

## Back up all databases

Use the [`ysql_dumpall`](../../../admin/ysql-dumpall) backup utility to write out (or "dump") all YugabyteDB databases of a universe into a single SQL script file. The script file is a plain-text file that contains SQL statements that can be used as input to `ysqlsh` to restore the databases. `ysql_dumpall` uses calls to `ysql_dump` for each database in the universe. Unlike the `ysql_dump` utility, the `ysql_dumpall` utility backs up all database objects, including roles, databases, schemas, tables, indexes, triggers, functions, constraints, ownerships, and privileges.

{{< note title="Note" >}}

*If you have enabled password authentication*, you will be prompted for a password as each database in your YugabyteDB universe is backed up because `ysql_dumpall` connects once per database. To have your backup proceed uninterrupted, you can create a `~/.pgpass` file.

{{< /note >}}

To back up all databases as the `yugabyte` user, follow these steps:

1. Open a command line window and change to your YugabyteDB home directory. For this example, the home directory is `yugabyte`.

2. Run the following command to back up all databases and database objects to the specified SQL script file.

```sh
$ ./postgres/bin/ysql_dumpall -U yugabyte > ../backup/yb-all.sql
```

Options for the `ysql_dumpall` utility can be used to limit backups to roles only (`--roles-only`), all database objects except the data (`--schema-only`), or other options. For details about the options available with the `ysql_dumpall` utility, see [`ysql_dumpall`](../../../admin/ysql_dumpall`).