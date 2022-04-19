---
title: Export and import data for YSQL
headerTitle: Export and import data
linkTitle: Export and import data
description: Export and import data for YSQL
aliases:
  - /preview/manage/backup-restore/back-up-data/
  - /preview/manage/backup-restore/restore-data/
menu:
  preview:
    identifier: export-import-data
    parent: backup-restore
    weight: 703
type: page
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../export-import-data/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../export-import-data-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

YugabyteDB comes with two utilities – `ysql_dump` and `ysql_dumpall` – that provide functionality to export the data into a SQL script. These utilities derive from PostgreSQL `pg_dump` and `pg_dumpall`.

In general, you should use other ways to take backups, such as the [distributed backup and recovery](../snapshot-ysql/) capability. However, it makes sense to use `ysql_dump` and `ysql_dumpall` if you intend to restore the data on a database other than YugabyteDB, or if having data in SQL format is a requirement for other reasons, such as regulations.

Both YSQL dump tools are thread-safe, so they will always give you a consistent version of the database even if run concurrently with other applications that read and update the data.

## Export a single database

To export a single database with all its tables, indexes, and so on, use the `ysql_dump` utility.

{{< note title="Note" >}}

`ysql_dump` does NOT export global entities like users, roles, permissions, etc. Use the `ysql_dumpall` utility (described below) if you want to include those.

{{< /note >}}

To do the export, run the following command:

```sh
$ ./postgres/bin/ysql_dump -d <db-name> > <file>
```

Where:

* `<db-name>` is the name of the database to be exported.
* `<file>` is the path to the resulting SQL script file.

For example, to export the `mydb` database into a file called `mydb-dump.sql` located in the `backup` folder, the command would be as follows:

```sh
$ ./postgres/bin/ysql_dump -d mydb > backup/mydb-dump.sql
```

For more details, and a list of all available options, see the [`ysql_dump` reference](../../../admin/ysql-dump/).

## Export all databases

To export all databases, along with the global entities (users, roles, permissions, and so on), use the `ysql_dumpall` utility.

To do the export, run the following command:

```sh
$ ./postgres/bin/ysql_dumpall > <file>
```

Where `<file>` is the path to the resulting SQL script file.

Two of the script's common command-line options are:

* `--roles-only` exports just the roles.
* `--schema-only` exports all database objects, _without_ the data.

For more details, and a list of all available options, see the [`ysql_dumpall` reference](../../../admin/ysql-dumpall/).

## Import

You can import schemas and objects into YugabyteDB from an SQL script. You can create such a script using the `ysql_dump` or `ysql_dumpall` tool as described above, or obtain one from an external database that supports PostgreSQL syntax.

To do the import, use the `ysqlsh` command line tool:

```sh
$ ./bin/ysqlsh -f <sql-script>
```

Where `<sql-script>` is the path to the SQL script to be imported.

You can also use the `\i` command in the `ysqlsh` shell to import an SQL script:

```sql
yugabyte=# \i <sql-script>
```
