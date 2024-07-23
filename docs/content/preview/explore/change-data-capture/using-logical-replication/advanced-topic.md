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

In this section, we explore a range of topics that are designed to provide deeper insights and enhance your understanding of advanced functionalities. 

## Schema evolution

A change in the schema of the tables (Alter Table) being streamed is transparently handled by the database without manual intervention.

This is illustrated in the below example. Note that the client being used for example purposes is `pg_recvlogical` which is discussed in [this section](../using-logical-replication/get-started).

```sh
# Create table and create the replication slot. pg_recvlogical uses test_decoding output plugin by default.
CREATE TABLE demo_table (id INT PRIMARY KEY);

pg_recvlogical -d yugabyte --slot=demo_slot --create-slot

# Start streaming the changes from the replication slot.
pg_recvlogical -d yugabyte --slot=demo_slot --start -f -

# In a new shell, start ysqlsh and insert some data.
bin/ysqlsh

INSERT INTO demo_table VALUES (1);
INSERT INTO demo_table VALUES (2);

# pg_recvlogical would receive the inserts and print it on the console.
BEGIN 2
TABLE public.demo_table: INSERT: id[integer]:1
COMMIT 2
BEGIN 3
TABLE public.demo_table: INSERT: id[integer]:2
COMMIT 3

# Add a new column to the demo_table and insert some more rows.
ALTER TABLE demo_table ADD COLUMN address TEXT;
INSERT INTO demo_table VALUES (3, 'address1');
INSERT INTO demo_table VALUES (4, 'address2');

# Without any manual intervention pg_recvlogical would receive the inserts with the new schema and print it on the console.
BEGIN 4
TABLE public.demo_table: INSERT: id[integer]:3 col_text[text]:'address1'
COMMIT 4
BEGIN 5
TABLE public.demo_table: INSERT: id[integer]:4 col_text[text]:'address2'
COMMIT 5
```


## Addition of tables to publication

Addition of tables to the streaming list after slot creation is currently a preview feature. In order to enable dynamic table addition the tserver preview flag [cdcsdk_enable_dynamic_table_support](../../../../reference/configuration/yb-tserver/#cdcsdk_enable_dynamic_table_support) should be set to true.

The Publication’s tables list can change in two cases. The first case is when a previously created table is added to the publication by performing an alter publication. 

```sql
CREATE TABLE test_table_1(id INT PRIMARY KEY, aa INT, bb INT);
CREATE TABLE test_table_2(id INT PRIMARY KEY, aa INT, bb INT);

CREATE PUBLICATION PUB FOR TABLE test_table_1;

-- Start consumption through a replication slot.

ALTER PUBLICATION ADD TABLE test_table_2;
```

The second case is when a dynamically created table is added to `ALL TABLES` publication upon creation.

```sql
CREATE TABLE test_table_1(id INT PRIMARY KEY, aa INT, bb INT);

CREATE PUBLICATION PUB FOR ALL TABLES;

-- Start consumption through a replication slot.

CREATE TABLE test_table_2(id INT PRIMARY KEY, aa INT, bb INT);
```

### YugabyteDB semantics
Unlike Postgres, any changes made to the publication’s tables list will not be applied immediately in YugabyteDB. Instead the publication’s tables list will be periodically refreshed and changes, if any, will be applied. The refresh interval is denoted by the flag [cdcsdk_publication_list_refresh_interval_secs](../../../../reference/configuration/yb-tserver/#cdcsdk_publication_list_refresh_interval_secs) and has a default value of one hour (3600 sec). This means that any changes made to the publication’s tables list will be applied after `cdcsdk_publication_list_refresh_interval_secs` in the worst case. Consider the following example:

```
Suppose that the value of the flag cdcsdk_publication_list_refresh_interval_secs is 3600 sec (1 hour) and the publication's tables list is being refreshed every hour at 8 am, 9 am, 10 am...

If any change is made to publication's tables list at 8:01 am, then this change will be applied at 9:00 am. However any change made to publication's tables list at 8:59 am, will also be applied at 9:00 am.
```

The value of this flag can be changed at run time, but the change will become effective only after some time. Continuing from above example:

```
Suppose that the value of the flag cdcsdk_publication_list_refresh_interval_secs is changed from 3600 sec (1 hour) to 600 sec (10 mins) at 8:01 am.

This change will only be applied after 9:00 am, i.e the publication's tables list will be next refreshed at 9:00 am. However the next refresh will happen at 9:10 am, and the subsequent refreshes will take place every 10 minutes.
```

### Required settings

To enable dynamic table addition, perform the following steps:

1. In order to enable dynamic table addition the tserver preview flag [cdcsdk_enable_dynamic_table_support](../../../../reference/configuration/yb-tserver/#cdcsdk_enable_dynamic_table_support) should be set to true. 

   - Since it's a preview flag, we'll have to first add it to the `allowed_preview_flags_csv` list to be able to set it. 

      ```sh
      ./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag allowed_preview_flags_csv cdcsdk_enable_dynamic_table_support
      ```

    - Set the flag `cdcsdk_enable_dynamic_table_support` to true.

      ```sh
      ./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag cdcsdk_enable_dynamic_table_support true
      ```

2. Set the flag [cdcsdk_publication_list_refresh_interval_secs](../../../../reference/configuration/yb-tserver/#cdcsdk_publication_list_refresh_interval_secs) to a lower value like 60 or 120 seconds. Note that the effect of this setting would take place after the upcoming publication refresh is performed (as explained in the example in the previous sub-section).

```sh
./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag cdcsdk_publication_list_refresh_interval_secs 120
```

3. Once you start receiving records from the newly added table in the publication, reset the flag `cdcsdk_publication_list_refresh_interval_secs` to a high value (e.g. 3600 seconds).

```sh
./yb-ts-cli --server_address=<tserverIpAddress:tserverPort> set_flag cdcsdk_publication_list_refresh_interval_secs 3600
```
## Initial snapshot

The [initial snapshot](../../../architecture/docdb-replication/cdc-logical-replication#initial-snapshot) data for a table is consumed by executing a snapshot query (SELECT statement). To ensure that the streaming phase continues exactly from where the snapshot left, this snapshot query is executed as of a specific database state. In YugabyteDB, this database state is represented by a value of `HybridTime`. Changes due to transactions with commit time strictly greater than this snapshot `HybridTime` will be consumed during the streaming phase.

The consistent database state on which the snapshot query is to be executed is specified using the following command -

```sql
SET LOCAL yb_read_time TO '<consistent_point commit time> ht';
```
		
This command should first be executed on the connection (session). The SELECT statement corresponding to the snapshot query should then be executed as part of the same transaction. The HybridTime value to use in the `SET LOCAL yb_read_time` command is the value of the `snapshot_name` field that is returned by the [CREATE_REPLICATION_SLOT](../../../api/ysql/the-sql-language/statements/#create-replication-slot) command. This value can also be be obtained by executing the following query:

```sql
select yb_restart_commit_ht
from pg_replication_slots where slot_name = <slot_name>;
```

{{< note Title="Explore" >}}

For more details on `pg_replication_slots` catalog view, please refer the [pg_replication_slots](../using-logical-replication/monitor#pg-replication-slots) section.

{{< /note >}}

{{< note title="Note" >}}

Only a superuser can execute the command to set the value of the `yb_read_time`.

{{< /note >}}

For a non-super user to be able to perform an initial snapshot, the following YugabyteDB specific additional setup is required (apart from granting the required SELECT and USAGE privileges)

```sql
-- Login as superuser

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
CALL set_yb_read_time(‘<consistent_point commit time> ht’)
```