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

