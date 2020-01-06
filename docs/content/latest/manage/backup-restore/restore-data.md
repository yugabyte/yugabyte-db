---
title: Restore data
linkTitle: Restore data
description: Restore data
image: /images/section_icons/manage/enterprise.png
headcontent: Restore data in YugabyteDB.
aliases:
  - /manage/backup-restore/backing-up-data
menu:
  latest:
    identifier: restore-data
    parent: manage-backup-restore
    weight: 703
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="/latest/manage/backup-restore/restore-data" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="/latest/manage/backup-restore/restore-data-ycql" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

To restore your YugabyteDB databases that were backed up using the `ysql_dump` and `ysql_dumpall` backup utilities, you can use the `ysqlsh` to restore one or all databases.

## Restore databases using `ysqlsh \i`

To restore one database using the `ysqlsh` shell, follow these steps:

1. Open a command line window and change to your YugabyteDB home directory.

2. Run the following command to open the `ysqlsh` command line interface.

```sh
$ ./bin/ysqlsh
```

```
ysqlsh (11.2-YB-2.0.9.0-b0)
Type "help" for help.

yugabyte=# 
```

3. At the `ysqlsh` shell prompt, run the following command.

```postgresql
yugabyte=# \i <db-sql-script>
```

- *db-sql-script*: the SQL script file dumped using `ysql_dump`

{{< note title="Note" >}}

To reduce security risks, SQL script files created using `ysql_dump` include `SELECT pg_catalog.set_config('search_path', '', false);` to set the `search_path` to an empty string. As a result, when you restore a database from the `ysqlsh` shell, running the `\d` command returns "Did not find any relations." To see the restored database, set the `search_path` again to expected schema to see the restored database.

{{< /note >}}

## Restore databases using `ysqlsh -f`

To restore one database using `ysqlsh` on the command line, open a command line window and run the following command. The `ysqlsh -f` command will read the SQL statements from the specified file and 

```sh
$ ysqlsh -f <db-sql-script> >
```

## Restore all databases

