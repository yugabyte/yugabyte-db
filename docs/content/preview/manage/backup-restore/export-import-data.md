---
title: Export and Import Data for YSQL
headerTitle: Export and Import Data
linkTitle: Export and Import Data
description: Export and Import Data for YSQL
menu:
  latest:
    identifier: export-import-data
    parent: backup-restore
    weight: 703
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

Note that generally it’s recommended to use other ways to take backups, like the Distributed Backup and Recovery capability. However, it does make sense to use `ysql_dump` and `ysql_dumpall` if you intend to restore the data on a database other than YugabyteDB, or if having data in SQL format is a requirement for other external reasons (e.g., regulations).

Both tools are thread-safe, so they will always give you a consistent cut of the database even if run concurrently with other applications that read and update the data.

## Export a Single Database

To export a single database with all its tables, indexes, etc., use the `ysql_dump` utility.

{{< note title="Note" >}}

`ysql_dump` does NOT export global entities like users, roles, permissions, etc. Use the `ysql_dumpall` utility (described below) if you want to include those.

{{< /note >}}

To do the export, run the following command:

```sh
$ ./postgres/bin/ysql_dump -d <db-name> > <file>
```

Where:
- `<db-name>` – the name of the database to be exported
- `<file>` – the path to the resulting SQL script file

For example, if you’re exporting `mydb` database into `mydb-dump.sql` file located in `backup` folder, the command will look like this:

```sh
$ ./postgres/bin/ysql_dump -d mydb > backup/mydb-dump.sql
```

For more details and the list of optional parameters, see the [`ysql_dump` reference](../../../admin/ysql-dump/).

## Export All Databases

To export all databases, along with the global entities (users, roles, permissions, etc.), use the `ysql_dumpall` utility.

To do the export, run the following command:

```sh
$ ./postgres/bin/ysql_dumpall > <file>
```

Where:
- `<file>` – the path to the resulting SQL script file

You can also use the `ysql_dumpall` to export roles only (`--roles-only` option), all database objects without the data (`--schema-only` option), as well as other cases. For more details and a list of available options, see the [`ysql_dumpall` reference](../../../admin/ysql-dumpall/).

## Import

YugabyteDB allows importing schemas and objects from a SQL script. Such a script can be either created by the `ysql_dump` or `ysql_dumpall` tool as described above, or provided by an external database that supports PostgreSQL syntax.

To do the import, use the `ysqlsh` command line tool:

```sh
$ ./bin/ysqlsh -f <sql-script>
```

Where:
- `<sql-script>` - path to the SQL script for importing

You can also achieve the same result with the `\i` command if running inside the `ysqlsh` shell:

```sh
$ ./bin/ysqlsh
ysqlsh (11.2-YB-2.11.2.0-b0)
Type "help" for help.

yugabyte=# \i <sql-script>
```
