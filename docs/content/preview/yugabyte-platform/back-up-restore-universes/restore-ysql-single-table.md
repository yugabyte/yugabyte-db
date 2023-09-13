---
title: Restore a single YSQL table
headerTitle: Restore a single YSQL table
linkTitle: Restore a single YSQL table
description: Use YugabyteDB Anywhere to restore data.
headContent: Restore from full or incremental backups
menu:
  preview_yugabyte-platform:
    parent: restore-universe-data
    identifier: restore-ysql-single-table
    weight: 30
type: docs
---

Currently, YugabyteDB Anywhere supports backing up and restoring entire YSQL databases only. However, after the database restore, you can perform some manual steps to restore single tables to the target database.

The scope of the following procedure is limited to restoring single table that do not have foreign key relations with other tables. In general, restoring a single tables with relations with other tables is difficult, owing to the fact that you may encounter referential integrity issues.

## Example

The following example assumes the following basic setup:

- a YSQL database named source_db.
- source_db has 3 tables: table_1, table_2, and table_3; the tables do not have any foreign key references.
- source_db has been backed up using YugabyteDB Anywhere.

To restore a single table to source_db, you would perform the following steps:

- Restore the backup of source_db to a new database (called restored_db in the example). This can be done on the same or a different universe.
- Use ysql_dump to create an SQL script file of the table from the restored_db database.
- Drop the table present with the same name in the source_db database. This is required to prevent overwriting of data. (This step is only required if the table with the same name exists on the database.)
- Apply the exported file to the source_db database using ysqlsh meta-commands.

### Restore the backup to a new database

You can restore a backup to the same or to a different universe.

During the restore, rename the database. For example, you would rename source_db to restored_db. Because YugabyteDB Anywhere backs up and restores only full databases in YSQL, restored_db has the same tables and data as source_db at the time of backup.

### Create a single table export from restored database

To create the export file for the table you want to restore, you use ysql_dump. ysql_dump is located in the `postgres/bin` directory of the YugabyteDB home directory.

Do the following to create export of the table_1 table from the restored database restored_db:

1. Connect to one of the nodes of the universe where you restored the backup (that is, the universe with restored_db) and change to the directory where ysql_dump is located. For example:

    ```sh
    [yugabyte@ip-172-151-36-174 ~]$ cd /home/yugabyte/tserver/postgres/bin
    ```

1. Use ysql_dump to export the table to an SQL script file. The syntax used for dumping a single table is as follows:

    ```sh
    [yugabyte@ip-172-151-36-174 ~]$ ./ysql_dump -h <host> -t <table-name> <database-name> > <file-path>
    ```

    Replace values as follows:

    - host: The host IP of DB node.
    - table-name: The name of the table (table_1).
    - database-name: The name of the database (restored_db).
    - file-path: The file path for storing the command output, which is an SQL script.

    For example:

    ```sh
    [yugabyte@ip-172-151-36-174 ~]$ ./ysql_dump -h 172.151.36.174 -t table_1 restored_db > /home/yugabyte/restore_db_table_1.sql
    ```

### Drop the table from the source

Before you can restore the table, you need to drop the existing table in the source_db database. To do this, you use ysqlsh, located in the `/bin` directory of the YugabyteDB home directory.

Do the following to drop a table from the source database source_db:

1. Connect to one of the nodes of the universe with the source database (that is, the universe with source_db) and start ysqlsh. For example:

    ```sh
    [yugabyte@ip-172-151-36-174 ~]$ tserver/bin/ysqlsh -h 172.151.36.174
    ```

1. Connect to the database source_db:

    ```sh
    yugabyte=# \c source_db
    ```

1. Drop table table_1:

    ```sh
    source_db=# DROP TABLE table_1;
    ```

    ```output
    DROP TABLE
    ```

    ```sh
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

### Apply the script to source database

You apply the SQL file generated using ysql_dump using the ysqlsh `\i` meta-command. For example:

```sh
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
