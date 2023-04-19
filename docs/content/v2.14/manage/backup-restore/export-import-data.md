---
title: Export and import for YSQL
headerTitle: Export and import
linkTitle: Export and import
description: Export and import for YSQL
menu:
  v2.14:
    identifier: export-import-data
    parent: backup-restore
    weight: 703
type: docs
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

YugabyteDB includes `ysql_dump` and `ysql_dumpall` utilities that allow you to export data into a SQL script. These utilities derive from PostgreSQL `pg_dump` and `pg_dumpall`.

In general, it is recommended to use other means for backups, such as [distributed backup and recovery](../snapshot-ysql/). However, you can use `ysql_dump` and `ysql_dumpall` if you intend to restore the data on a database other than YugabyteDB, or if having data in SQL format is a requirement for other reasons, such as regulations.

Both utilities are thread-safe and always produce a consistent version of the database even if run concurrently with other applications that read and update the data.

## Export a single database

To export a single database with all its tables, indexes, and other local artifacts, use the `ysql_dump` utility by executing the following command:

```sh
./postgres/bin/ysql_dump -d <db-name> > <file>
```

* *db-name* is the name of the database to be exported.
* *file* is the path to the resulting SQL script file.

For example, to export the `mydb` database into a file called `mydb-dump.sql` located in the `backup` folder, the command would be as follows:

```sh
./postgres/bin/ysql_dump -d mydb > backup/mydb-dump.sql
```

For more details and a list of all available options, see [ysql_dump reference](../../../admin/ysql-dump/).

## Export all databases

To export all databases, along with the global artifacts such as users, roles, permissions, and so on, use the `ysql_dumpall` utility by executing the following command:

```sh
./postgres/bin/ysql_dumpall > <file>
```

*file* is the path to the resulting SQL script file.

The following are two of the script's common command-line options:

* *--roles-only* exports just the roles.
* *--schema-only* exports all database objects without the data.

For more details and a list of all available options, see [ysql_dumpall reference](../../../admin/ysql-dumpall/).

## Import

You can import schemas and objects into YugabyteDB from a SQL script. To create this script, follow instructions provided in [Export a single database](#export-a-single-database) or [Export all databases](#export-all-databases). Alternatively, you can obtain the script from an external database that supports PostgreSQL syntax.

To import the script, use the `ysqlsh` command line tool, as follows:

```sh
./bin/ysqlsh -f <sql_script>
```

*sql_script* is the path to the SQL script to be imported.

You can also use the `\i` command in the `ysqlsh` shell to import an SQL script, as follows:

```sql
yugabyte=# \i <sql_script>
```
