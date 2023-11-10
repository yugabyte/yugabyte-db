---
title: Restore a single YSQL table
headerTitle: Restore a single YSQL table
linkTitle: Restore a single YSQL table
description: Use YugabyteDB Anywhere to restore data.
headContent: Restore from full or incremental backups
menu:
  v2.18_yugabyte-platform:
    parent: restore-universe-data
    identifier: restore-ysql-single-table
    weight: 30
type: docs
---

Currently, YugabyteDB Anywhere supports backing up and restoring entire YSQL databases only. However, by performing some manual steps, you can restore single tables to a YSQL database using the following tools:

- [ysql_dump](../../../manage/backup-restore/export-import-data/) - exports data from a YSQL database into a SQL script file.
- [ysqlsh](../../../admin/ysqlsh/) - YugabyteDB SQL shell for interacting with YugabyteDB using YSQL.

Suppose you have a problem with a single table in a database called source_db, and you want to restore the table from a backup taken previously. You would perform the following steps:

1. Using a backup of source_db, restore the backup to a new database (call it restored_db). This can be done on the same or a different universe.
1. In restored_db, use ysql_dump to export a SQL script file of the table to be restored.
1. In source_db, use ysqlsh to drop the table with the same name (that is, the table you want restored from the backup). (You only need to do this if the table with the same name exists on the database.)
1. Use ysqlsh to import the SQL script file to the source_db database.

The scope of the following procedure is limited to restoring single tables that don't have foreign key relations with other tables. In general, restoring single tables with relations with other tables is difficult, owing to the fact that you may encounter referential integrity issues.

## Example

The following example assumes the following basic setup:

- a YSQL database named source_db.
- source_db has 3 tables (table_1, table_2, and table_3) and the tables do not have any foreign key references.
- source_db has been backed up using YugabyteDB Anywhere.

### Restore the backup to a new database

You can restore the backup to the same or to a different universe. This example assumes that you restored to the *same* universe.

During the restore, rename the database. For example, you would rename source_db to restored_db. Because YugabyteDB Anywhere backs up and restores only full databases in YSQL, restored_db has the same tables and data as source_db at the time of backup.

### Export a single table from the restored database

Use ysql_dump to create the script file for the table you want to restore. ysql_dump is located in the `postgres/bin` directory of the YugabyteDB home directory on any of the nodes of your universe.

Do the following:

1. Connect to one of the nodes of the universe where you restored the backup (that is, the universe with restored_db) and change to the directory where ysql_dump is located. For example:

    ```sh
    cd /home/yugabyte/tserver/postgres/bin
    ```

1. Use ysql_dump to export the table to a SQL script file. The syntax for dumping a single table is as follows:

    ```sh
    ./ysql_dump -h <host> -t <table-name> <database-name> -f <file-path>
    ```

    Replace values as follows:

    - *host* is the host IP of the universe node.
    - *table-name* is the name of the table (table_1) to export.
    - *database-name* is the name of the database (restored_db).
    - *file-path* is the path to the resulting SQL script file.

    For example:

    ```sh
    ./ysql_dump -h 172.16.0.0 -t table_1 restored_db -f /home/yugabyte/restored_db_table_1.sql
    ```

### Drop the table from the source

Before you can restore the table, you need to drop table_1 from the source_db database. To do this, you can use ysqlsh, located in the `/bin` directory of the YugabyteDB home directory.

Do the following to drop a table from the source database source_db:

1. Connect to one of the nodes of the universe with the source database (that is, the universe with source_db) and start ysqlsh. For example:

    ```sh
    ./tserver/bin/ysqlsh -h 172.16.0.0
    ```

1. Connect to the source_db database:

    ```sql
    yugabyte=# \c source_db
    ```

1. Drop table table_1:

    ```sql
    source_db=# DROP TABLE table_1;
    ```

    ```output
    DROP TABLE
    ```

    ```sql
    source_db=# \d
    ```

    ```output
             List of relations
     Schema |  Name   | Type  |  Owner   
    --------+---------+-------+----------
     public | table_1 | table | yugabyte
     public | table_2 | table | yugabyte
     (2 rows)
    ```

### Import the script to source database

You import the SQL script file to the source_db database using the ysqlsh `\i` meta-command. For example, assuming restored_db was restored to the universe with source_db, do the following:

```sql
source_db=# \i /home/yugabyte/restore_db_table_1.sql
source_db=# \d
```

```output
          List of relations
 Schema |  Name   | Type  |  Owner
--------+---------+-------+----------
 public | table_1 | table | yugabyte
 public | table_2 | table | yugabyte
 public | table_3 | table | yugabyte
(3 rows)
```
