---
title: Advanced topics
headerTitle: Advanced topics
linkTitle: Advanced topics
description: Advanced topics for Change Data Capture in YugabyteDB.
menu:
  preview:
    parent: explore-change-data-capture-logical-replication
    identifier: advanced-topics
    weight: 50
type: docs
---

This section explores a range of topics designed to provide deeper insights and enhance your understanding of advanced functionalities.

## Schema evolution

A change in the schema of the tables (ALTER TABLE) being streamed is transparently handled by the database without manual intervention.

This is illustrated in the following example. The client used for the example is [pg_recvlogical](../get-started/#getting-started-with-pg-recvlogical).

1. Create a table and create the replication slot. pg_recvlogical uses the test_decoding output plugin by default.

    ```sql
    CREATE TABLE demo_table (id INT PRIMARY KEY);
    ```

    ```sh
    pg_recvlogical -d yugabyte --slot=demo_slot --create-slot
    ```

1. Start streaming the changes from the replication slot.

    ```sh
    pg_recvlogical -d yugabyte --slot=demo_slot --start -f -
    ```

1. In a new shell, start ysqlsh and insert some data.

    ```sh
    bin/ysqlsh
    ```

    ```sql
    INSERT INTO demo_table VALUES (1);
    INSERT INTO demo_table VALUES (2);
    ```

    pg_recvlogical receives the inserts and prints it on the console.

    ```output
    BEGIN 2
    TABLE public.demo_table: INSERT: id[integer]:1
    COMMIT 2
    BEGIN 3
    TABLE public.demo_table: INSERT: id[integer]:2
    COMMIT 3
    ```

1. Add a new column to the demo_table and insert some more rows.

    ```sql
    ALTER TABLE demo_table ADD COLUMN address TEXT;
    INSERT INTO demo_table VALUES (3, 'address1');
    INSERT INTO demo_table VALUES (4, 'address2');
    ```

    Without any manual intervention, pg_recvlogical receives the inserts with the new schema and prints it on the console.

    ```output
    BEGIN 4
    TABLE public.demo_table: INSERT: id[integer]:3 col_text[text]:'address1'
    COMMIT 4
    BEGIN 5
    TABLE public.demo_table: INSERT: id[integer]:4 col_text[text]:'address2'
    COMMIT 5
    ```

## Adding tables to publication

Addition of tables to the streaming list after slot creation is currently a preview feature. To enable dynamic table addition, set the [cdcsdk_enable_dynamic_table_support](../../../../reference/configuration/yb-tserver/#cdcsdk_enable_dynamic_table_support) flag to true.

The Publication's tables list can change in two ways. The first way is by adding a table to the publication by performing an alter publication.

```sql
CREATE TABLE test_table_1(id INT PRIMARY KEY, aa INT, bb INT);
CREATE TABLE test_table_2(id INT PRIMARY KEY, aa INT, bb INT);

CREATE PUBLICATION PUB FOR TABLE test_table_1;

-- Start consumption through a replication slot.

ALTER PUBLICATION ADD TABLE test_table_2;

CREATE TABLE test_table_3(id INT PRIMARY KEY, aa INT, bb INT);

ALTER PUBLICATION ADD TABLE test_table_3;
```

The second way is when a table is added to `ALL TABLES` publication upon creation.

```sql
CREATE TABLE test_table_1(id INT PRIMARY KEY, aa INT, bb INT);

CREATE PUBLICATION PUB FOR ALL TABLES;

-- Start consumption through a replication slot.

CREATE TABLE test_table_2(id INT PRIMARY KEY, aa INT, bb INT);
-- Since the publication was created for ALL TABLES, alter publication is not requirred.
```

### YugabyteDB semantics

Unlike PostgreSQL, any changes made to the publication's tables list are not applied immediately in YugabyteDB. Instead the publication's tables list is periodically refreshed, and changes, if any, are applied. The refresh interval is managed using the [cdcsdk_publication_list_refresh_interval_secs](../../../../reference/configuration/yb-tserver/#cdcsdk-publication-list-refresh-interval-secs) flag. The default is one hour (3600 sec). This means that any changes made to the publication's tables list will be applied after `cdcsdk_publication_list_refresh_interval_secs` in the worst case.

Consider the following example:

- Suppose that the value of the flag `cdcsdk_publication_list_refresh_interval_secs` is 3600 sec (1 hour) and the publication's tables list is being refreshed every hour at 8 am, 9 am, 10 am, and so on.

- If any change is made to publication's tables list at 8:01 am, then this change will be applied at 9:00 am. However, any change made to publication's tables list at 8:59 am will also be applied at 9:00 am.

The value of this flag can be changed at run time, but the change becomes effective only after some time. Continuing the example:

- Suppose that the value of the flag `cdcsdk_publication_list_refresh_interval_secs` is changed from 3600 sec (1 hour) to 600 sec (10 minutes) at 8:01 am.

- This change will only be applied after 9:00 am. That is, the publication's tables list will be next refreshed at 9:00 am. Then, the next refresh will happen at 9:10 am, and the subsequent refreshes will take place every 10 minutes.

### Required settings

To enable dynamic table addition, perform the following steps:

1. Set the [cdcsdk_enable_dynamic_table_support](../../../../reference/configuration/yb-tserver/#cdcsdk-enable-dynamic-table-support) to true.

    Because it is a preview flag, first add it to the `allowed_preview_flags_csv` list.

    ```sh
    ./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag allowed_preview_flags_csv cdcsdk_enable_dynamic_table_support
    ```

    Then set the `cdcsdk_enable_dynamic_table_support` flag to true.

    ```sh
    ./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag cdcsdk_enable_dynamic_table_support true
    ```

1. Set the [cdcsdk_publication_list_refresh_interval_secs](../../../../reference/configuration/yb-tserver/#cdcsdk-publication-list-refresh-interval-secs) flag to a lower value, such as 60 or 120 seconds. Note that the effect of this setting takes place after the upcoming publication refresh is performed.

    ```sh
    ./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag cdcsdk_publication_list_refresh_interval_secs 120
    ```

1. After you start receiving records from the newly added table in the publication, reset the  `cdcsdk_publication_list_refresh_interval_secs` flag to a high value (for example, 3600 seconds).

    ```sh
    ./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag cdcsdk_publication_list_refresh_interval_secs 3600
    ```

## Initial snapshot

The [initial snapshot](../../../../architecture/docdb-replication/cdc-logical-replication/#initial-snapshot) data for a table is consumed by executing a snapshot query (SELECT statement). To ensure that the streaming phase continues exactly from where the snapshot left, this snapshot query is executed as of a specific database state. In YugabyteDB, this database state is represented by a value of `HybridTime`. Changes due to transactions with commit time strictly greater than this snapshot `HybridTime` will be consumed during the streaming phase.

The consistent database state on which the snapshot query is to be executed is specified using the following command:

```sql
SET LOCAL yb_read_time TO '<consistent_point commit time> ht';
```

This command should first be executed on the connection (session). The SELECT statement corresponding to the snapshot query should then be executed as part of the same transaction. The HybridTime value to use in the `SET LOCAL yb_read_time` command is the value of the `snapshot_name` field that is returned by the [CREATE REPLICATION SLOT](../../../../api/ysql/the-sql-language/statements/#create-replication-slot) command.

You can also obtain this value by executing the following query:

```sql
select yb_restart_commit_ht
from pg_replication_slots where slot_name = <slot_name>;
```

For more information on the `pg_replication_slots` catalog view, refer to [pg_replication_slots](../monitor/#pg-replication-slots).

### Permissions

Only a superuser can execute the command to set the value of `yb_read_time`.

For a non-superuser to be able to perform an initial snapshot, perform the following additional setup as a superuser (in addition to granting the required SELECT and USAGE privileges):

```sql
CREATE ROLE appuser WITH LOGIN REPLICATION;
CREATE SCHEMA appuser AUTHORIZATION appuser;

CREATE OR REPLACE PROCEDURE appuser.set_yb_read_time(value TEXT)
LANGUAGE plpgsql
AS $$
BEGIN
  EXECUTE 'SET LOCAL yb_read_time = ' || quote_literal(value);
END;
$$
SECURITY DEFINER;


REVOKE EXECUTE ON PROCEDURE appuser.set_yb_read_time FROM PUBLIC; 
GRANT EXECUTE ON PROCEDURE appuser.set_yb_read_time TO appuser;
```

With this setup, the command to be executed by the application user as part of the transaction prior to executing the snapshot SELECT query would be:

```sh
CALL set_yb_read_time('<consistent_point commit time> ht')
```
