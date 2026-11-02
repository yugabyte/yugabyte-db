---
title: Advanced topics
headerTitle: Advanced topics
linkTitle: Advanced topics
description: Advanced topics for Change Data Capture in YugabyteDB.
aliases:
  - /stable/explore/change-data-capture/using-logical-replication/advanced-topic/
menu:
  stable:
    parent: explore-change-data-capture-logical-replication
    identifier: advanced-topics
    weight: 50
type: docs
---

This section explores a range of topics designed to provide deeper insights and enhance your understanding of advanced functionalities.

## Schema evolution

Logical replication keeps streaming while the schema of the published tables, or the publication itself, changes. You do not drop or recreate the replication slot. This page describes the current behavior. For how it differs across releases and from PostgreSQL, see [Behavior by release](../behavior-by-release/).

### Column changes

A change to the columns of a streamed table is handled by the database without manual intervention. The replication protocol has no separate DDL message. As in PostgreSQL, the output plugin sends the table's updated relation metadata (the `Relation` message in pgoutput and yboutput) before the first change that uses the new schema. Changes committed before the `ALTER TABLE` are decoded with the old schema.

This is illustrated in the following example. The client used for the example is [pg_recvlogical](../../../../explore/change-data-capture/#try-it-out).

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

    pg_recvlogical receives the inserts and prints them on the console.

    ```output
    BEGIN 2
    TABLE public.demo_table: INSERT: id[integer]:1
    COMMIT 2
    BEGIN 3
    TABLE public.demo_table: INSERT: id[integer]:2
    COMMIT 3
    ```

1. Add a new column to `demo_table` and insert some more rows.

    ```sql
    ALTER TABLE demo_table ADD COLUMN address TEXT;
    INSERT INTO demo_table VALUES (3, 'address1');
    INSERT INTO demo_table VALUES (4, 'address2');
    ```

    Without any manual intervention, pg_recvlogical receives the inserts with the new schema and prints them on the console.

    ```output
    BEGIN 4
    TABLE public.demo_table: INSERT: id[integer]:3 address[text]:'address1'
    COMMIT 4
    BEGIN 5
    TABLE public.demo_table: INSERT: id[integer]:4 address[text]:'address2'
    COMMIT 5
    ```

## Adding tables to publication

The publication's tables list can change in two ways. The first way is by adding a table to the publication by performing an `ALTER PUBLICATION`:

```sql
CREATE TABLE test_table_1(id INT PRIMARY KEY, aa INT, bb INT);
CREATE TABLE test_table_2(id INT PRIMARY KEY, aa INT, bb INT);

CREATE PUBLICATION PUB FOR TABLE test_table_1;

-- Start consumption through a replication slot.

ALTER PUBLICATION ADD TABLE test_table_2;

CREATE TABLE test_table_3(id INT PRIMARY KEY, aa INT, bb INT);

ALTER PUBLICATION ADD TABLE test_table_3;
```

The second way is when a table is added to an `ALL TABLES` publication upon creation:

```sql
CREATE TABLE test_table_1(id INT PRIMARY KEY, aa INT, bb INT);

CREATE PUBLICATION PUB FOR ALL TABLES;

-- Start consumption through a replication slot.

CREATE TABLE test_table_2(id INT PRIMARY KEY, aa INT, bb INT);
```

As the publication was created for ALL TABLES, ALTER PUBLICATION is not required.

When the publication changes, a replication stream applies the change to the set of tables it polls in one of two modes:

- **Immediate effect**, called [implicit publication](#implicit-publication). The change takes effect at the commit point of the `ALTER PUBLICATION` or `CREATE TABLE`.
- **Refresh interval**, called [periodic publication](#periodic-publication). The change takes effect at the next scheduled refresh of the publication's table list.

Implicit publication is the default. [Behavior by release](../behavior-by-release/#publication-change-detection) lists the default in each release. The mode is fixed for each replication slot when the slot is created. See [Slots using periodic publication](#slots-using-periodic-publication) for the consequences.

### Implicit publication

Implicit publication is the mechanism by which a logical replication stream learns that the set of tables in its publication has changed. The stream watches the system catalog as part of the change stream it is already reading, so an `ALTER PUBLICATION`, or the creation of a table in a `FOR ALL TABLES` publication, is detected at the commit time of that statement, in order with the surrounding changes. The stream then starts or stops polling the affected tables from exactly that point. No events from a newly added table are missed, and no events from a dropped table are sent, regardless of how far behind the consumer is. This behavior matches PostgreSQL.

Implicit publication is the default and applies only to the logical replication model of CDC. The alternative, used when it is disabled, is [periodic publication](#periodic-publication), where the stream re-reads the publication's table list on a timer. A slot's mode is fixed when the slot is created: a slot created with implicit publication cannot stream while the flag is `false`, and a slot created with periodic publication keeps it after the flag is set to `true`.

For example:

```sql
-- Assuming a replication slot is using the publication pub
ALTER PUBLICATION pub ADD TABLE employees;
INSERT INTO employees VALUES (1342, 'John Doe'); -- This will be captured by CDC

ALTER PUBLICATION pub DROP TABLE employees;
INSERT INTO employees VALUES (1343, 'Jane Doe'); -- This will NOT be captured by CDC
```

For architectural details, see [Publication change detection](../../../../architecture/docdb-replication/cdc-logical-replication/#detecting-publication-changes) in the logical replication architecture documentation.

Implicit publication is controlled by the following flags. Set them on both YB-Master and YB-TServer.

| Flag | Details |
| :--- | :--- |
| [ysql_yb_enable_implicit_dynamic_tables_logical_replication](../../../../reference/configuration/yb-tserver/#ysql-yb-enable-implicit-dynamic-tables-logical-replication) | When set to `true` (default), modifications to a publication are reflected implicitly, providing PostgreSQL-like semantics for dynamic tables. When set to `false`, the publication's tables list is [periodically refreshed](#periodic-publication). |
| cdc_enable_dynamic_schema_changes | Auto flag that guards feature deployment. This flag is automatically promoted as part of the upgrade process. The feature can be used only after this flag has been promoted. |

### Periodic publication

Periodic publication is the mechanism by which a logical replication stream learns of publication changes when [implicit publication](#implicit-publication) is disabled. The stream does not watch the catalog. Instead, on a fixed timer it re-reads the publication's table list, compares it with the list it is polling, and starts or stops polling the tables that differ. A change to the publication therefore takes effect only at the next refresh, not at the commit time of the `ALTER PUBLICATION`, and rows written to a newly added table before that refresh are not streamed. This behavior differs from PostgreSQL.

Periodic publication is used by every replication slot created while [ysql_yb_enable_implicit_dynamic_tables_logical_replication](../../../../reference/configuration/yb-tserver/#ysql-yb-enable-implicit-dynamic-tables-logical-replication) was `false`, or before an upgrade to v2026.1 was finalized. Setting the flag to `true` later does not change those slots.

You manage the refresh interval using the [cdcsdk_publication_list_refresh_interval_secs](../../../../reference/configuration/yb-tserver/#cdcsdk-publication-list-refresh-interval-secs) flag. This means that any changes made to the publication's tables list will be applied after `cdcsdk_publication_list_refresh_interval_secs` in the worst case. The default is 15 minutes (900 seconds).

To illustrate how this works, suppose `cdcsdk_publication_list_refresh_interval_secs` is set to 900 seconds (15 minutes) and the publication's tables list is being refreshed every 15 minutes at 8:00 am, 8:15 am, 8:30 am, and so on. A change made to the publication's tables list at 8:01 am will be applied at 8:15 am. Equally, a change made to the publication's tables list at 8:14 am will also be applied at 8:15 am.

You can change the refresh interval at run time, but the change becomes effective only after some time. For example, suppose you change `cdcsdk_publication_list_refresh_interval_secs` from 900 seconds (15 minutes) to 300 seconds (5 minutes) at 8:01 am. This change is only applied after 8:15 am. That is, the publication's tables list will next be refreshed at 8:15 am. Then the next refresh will happen at 8:20 am, and subsequent refreshes will take place every 5 minutes.

To enable dynamic table addition with minimal delay, perform the following steps:

1. Set the [cdcsdk_publication_list_refresh_interval_secs](../../../../reference/configuration/yb-tserver/#cdcsdk-publication-list-refresh-interval-secs) flag to a lower value, such as 60 or 120 seconds.

    ```sh
    ./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag cdcsdk_publication_list_refresh_interval_secs 120
    ```

    The new setting only takes place after the next publication refresh is performed.

1. After you start receiving records from the newly added table in the publication, reset `cdcsdk_publication_list_refresh_interval_secs` to its original value (that is, 900 seconds).

    ```sh
    ./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag cdcsdk_publication_list_refresh_interval_secs 900
    ```

{{< note title="Important" >}}
If you lower the value of `cdcsdk_publication_list_refresh_interval_secs`, you should set the value of the flag back to its original value after you start receiving changes from the new table, as every refresh incurs overhead.
{{< /note >}}

## Streaming DDLs causing table rewrite

A table rewrite replaces the table's tablets with new ones. `TRUNCATE` and `DROP TABLE` are handled by the same mechanism as an `ALTER TABLE` that [rewrites the table](../../../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-table-operations-that-involve-a-table-rewrite). On a _non-colocated_ table in a database with active logical replication, the DDL is allowed and the replication slots stay valid:

1. The old tablets are hidden rather than deleted.
1. Logical replication keeps streaming from the hidden tablets until every change committed before the DDL has been sent.
1. Streaming switches to the new tablets. For `DROP TABLE`, the table leaves the stream.
1. The hidden tablets are deleted after every replication slot in the database has acknowledged the changes committed before the DDL. A lagging consumer delays reclaiming this storage.

For architectural details, see [Table rewrite and DROP TABLE handling](../../../../architecture/docdb-replication/cdc-logical-replication/#table-rewrite-and-drop-table-handling).

### What the client receives

- **Rewriting `ALTER TABLE`**: the updated relation metadata, as for any [column change](#column-changes), followed by every existing row streamed again from the new tablets. Consumers must tolerate rows they have already received, for example by applying inserts as upserts. Tracked in {{<issue 31636>}}.
- **`TRUNCATE`**: no truncate record. Changes committed before the truncate that were not yet streamed are delivered first, followed by changes made after the truncate. The consumer cannot tell where the truncate happened, so an insert after the truncate that reuses a primary key arrives as an insert of a key the consumer already holds. Tracked in {{<issue 29674>}}.
- **`DROP TABLE`**: changes committed before the drop, after which no further records for the table are sent.

### Colocated tables

Rewriting `ALTER TABLE` and `TRUNCATE` on a _colocated_ table are blocked with an error while logical replication is active on the database. To perform such a DDL, drop the replication slots, run the DDL, and recreate the slots, potentially requiring a new CDC snapshot. Tracked in {{<issue 31908>}}.

### Slots using periodic publication

A replication slot created while [implicit publication](#implicit-publication) was disabled, or before an upgrade to v2026.1 was finalized (including slots created on v2026.1 binaries before the `cdc_enable_dynamic_schema_changes` auto flag was promoted), uses [periodic publication](#periodic-publication) for its lifetime. While such a slot exists on a table, rewriting `ALTER TABLE` and `TRUNCATE` on that table are blocked with an error that names the mechanism. To lift the restriction, drop and recreate those slots. Use [yb-admin list_cdc_streams](../../../../admin/yb-admin/#list-cdc-streams) to find streams whose `detect_publication_changes_implicitly` is not `true`.

### Flags

| Flag | Details |
| :--- | :--- |
| [enable_table_rewrite_for_cdcsdk_table](../../../../reference/configuration/yb-tserver/#enable-table-rewrite-for-cdcsdk-table) | YB-Master and YB-TServer flag. Default `true`. When `false`, rewriting `ALTER TABLE` and `TRUNCATE` are blocked with an error on tables with active logical replication, and a dropped table's tablets are not retained for CDC, so changes not yet streamed are lost. To perform a blocked DDL, drop the replication slots, run the DDL, and recreate the slots, potentially requiring a new CDC snapshot. |
| [enable_truncate_cdcsdk_table](../../../../reference/configuration/yb-master/#enable-truncate-cdcsdk-table) | YB-Master flag. Default `false`. When `true`, `TRUNCATE` is allowed on tables with an active stream regardless of the checks above. |
| cdc_enable_dynamic_schema_changes | Auto flag, not set by users; promoted when an upgrade to v2026.1 is finalized. Until it is promoted, rewriting `ALTER TABLE` and `TRUNCATE` are blocked with an error, and slots created in that window use [periodic publication](#slots-using-periodic-publication). |

## Initial snapshot

The [initial snapshot](../../../../architecture/docdb-replication/cdc-logical-replication/#initial-snapshot) data for a table is consumed by executing a snapshot query (SELECT statement). To ensure that the streaming phase continues exactly from where the snapshot left, this snapshot query is executed as of a specific database state. In YugabyteDB, this database state is represented by a value of `HybridTime`. Changes due to transactions with commit time strictly greater than this snapshot `HybridTime` will be consumed during the streaming phase.

The consistent database state on which the snapshot query is to be executed is specified using the following command:

```sql
SET LOCAL yb_read_time TO '<consistent_point commit time> ht';
```

This command should first be executed on the connection (session). The SELECT statement corresponding to the snapshot query should then be executed as part of the same transaction. The HybridTime value to use in the `SET LOCAL yb_read_time` command is the value of the `snapshot_name` field that is returned by the [CREATE REPLICATION SLOT](../../../../api/ysql/the-sql-language/statements/#streaming-replication-protocol-statements) command.

You can also obtain this value by executing the following query:

```sql
select yb_restart_commit_ht
from pg_replication_slots where slot_name = <slot_name>;
```

For more information on the `pg_replication_slots` catalog view, refer to [pg_replication_slots](../monitor/#pg-replication-slots).

### Using the HYBRID_TIME LSN

YugabyteDB currently supports two types of [LSN](../key-concepts/#lsn-type), SEQUENCE and HYBRID_TIME. In HYBRID_TIME mode, you can specify a hybrid time value `t` in the `pg_lsn` format and the replication stream will begin streaming transactions committed after `t`.

To obtain the current hybrid time value, use the `yb_get_current_hybrid_time_lsn()` function:

```sql
SELECT * FROM yb_get_current_hybrid_time_lsn();
```

This gives an output in terms of a long value. You can further convert this to `pg_lsn` format by defining the following method:

```sql
CREATE OR REPLACE FUNCTION get_current_lsn_format()
RETURNS text AS $$
DECLARE
    ht_lsn bigint;
    formatted_lsn text;
BEGIN
    SELECT yb_get_current_hybrid_time_lsn() INTO ht_lsn;
    SELECT UPPER(format('%s/%s', to_hex(ht_lsn >> 32), to_hex(ht_lsn & 4294967295)))
    INTO formatted_lsn;
    RETURN formatted_lsn;
END;
$$ LANGUAGE plpgsql;
```

Using the value from the method `get_current_lsn_format()`, you can now start your replication stream using:

```sh
START_REPLICATION SLOT rs LOGICAL 62D63025/5462E000;
```

{{< note title="Important" >}}

The replication slot being used must be created with LSN type `HYBRID_TIME`.

The `yb_get_current_hybrid_time_lsn()` function only works with LSN type `HYBRID_TIME`, and will not work with `SEQUENCE`.

{{< /note >}}

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

CREATE OR REPLACE PROCEDURE appuser.disable_catalog_version_check()
LANGUAGE plpgsql
AS $$
BEGIN
    EXECUTE 'SET yb_disable_catalog_version_check = true';
END;
$$
SECURITY DEFINER;

REVOKE EXECUTE ON PROCEDURE appuser.disable_catalog_version_check FROM PUBLIC;
GRANT EXECUTE ON PROCEDURE appuser.disable_catalog_version_check TO appuser;
```

With this setup, the command to be executed by the application user as part of the transaction prior to executing the snapshot SELECT query would be:

```sql
CALL set_yb_read_time('<consistent_point commit time> ht');
CALL disable_catalog_version_check();
```

## Replication origins

{{<tags/feature/ea idea="2525">}}Replication origins provide a mechanism to track the origin of changes in logical replication, which is essential for implementing bi-directional replication setups and preventing replication loops.

Replication origins are intended to make it easier to implement logical replication solutions on top of logical decoding. Using a replication origin, you can change replication behavior based on the origin of a row. This is particularly useful for preventing loops in bi-directional replication setups, where changes replicated from a remote node should not be replicated back to that same node.

In a bi-directional replication setup where two databases replicate changes to each other, replication origins help prevent infinite loops as follows:

1. When changes are applied on Database A that originated from Database B, those changes are tagged with Database B's replication origin.

1. When Database A streams changes to Database B, the output plugin can filter out changes that have Database B's replication origin, preventing those changes from being sent back to Database B.

This ensures that changes only flow in one direction.

The YugabyteDB implementation of replication origins mimics that of PostgreSQL; refer to [Replication Progress Tracking](https://www.postgresql.org/docs/15/replication-origins.html) in the PostgreSQL documentation.

### Properties

Replication origins have two properties:

| Property | Description |
| :--- | :--- |
| Name | Free-form text identifier that should be used to refer to the origin across systems. It should be used in a way that makes conflicts between replication origins created by different replication solutions unlikely (for example, by prefixing the replication solution's name to it). |
| ID | Internal identifier used to avoid having to store the long name in situations where space efficiency is important. The ID should never be shared across systems. |

### Create and manage replication origins

Create, drop, and query replication origins using the following functions:

- Create a replication origin:

    ```sql
    SELECT pg_replication_origin_create('origin_name');
    ```

    This function creates a new replication origin with the specified name and returns the assigned origin ID.

- Drop a replication origin:

    ```sql
    SELECT pg_replication_origin_drop('origin_name');
    ```

- View replication origins:

    Replication origins can be viewed in the `pg_replication_origin` [system catalog](../../../../architecture/system-catalog/):

    ```sql
    SELECT * FROM pg_replication_origin;
    ```

### Setting up replication origin for a session

To mark a session as replaying from a remote node, use the `pg_replication_origin_session_setup()` function:

```sql
SELECT pg_replication_origin_session_setup('origin_name');
```

When a replication origin is set up for a session, every change and transaction passed to output plugin callbacks generated by that session is tagged with the replication origin of the generating session. This allows output plugins to treat changes differently based on their origin, for example, by ignoring all but locally-originating rows.

To reset the replication origin for a session, use the following function:

```sql
SELECT pg_replication_origin_session_reset();
```

### How it works with the logical replication protocol

The PostgreSQL logical replication protocol streams transactions with origin information by including an optional meta message for each transaction if it has an origin set. This allows clients consuming the replication stream to:

- Identify the origin of each transaction
- Filter out transactions that originated from a specific remote node to prevent replication loops
- Implement different processing logic based on the transaction origin

When a session has a replication origin configured using `pg_replication_origin_session_setup()`, the origin information is included in the WAL records and streamed as part of the logical replication protocol. This enables output plugins and replication clients to detect the origin of events and avoid loops in bi-directional replication setups.
