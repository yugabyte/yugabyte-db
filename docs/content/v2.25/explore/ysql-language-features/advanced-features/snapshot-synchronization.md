---
title: Synchronize snapshots
linkTitle: Synchronize snapshots
description: Synchronize snapshots in YSQL
tags:
  feature: tech-preview
menu:
  v2.25:
    identifier: synchronize-snapshots
    parent: advanced-features
    weight: 800
type: docs
---

To maintain a consistent view of data, you can synchronize the snapshots from separate transactions. A snapshot determines which data is visible to the transaction that is using the snapshot. Synchronized snapshots are necessary when two or more sessions need to see identical content in the database.

When two sessions start transactions independently, there is always a possibility that some third transaction commits between the execution of the two START TRANSACTION commands, so that one session sees the effects of that transaction, and the other does not. To solve this problem, you can export the snapshot a transaction is using. As long as the exporting transaction remains open, other transactions can import its snapshot, and thereby be guaranteed that they see exactly the same view of the database that the first transaction sees.

Note that any database changes made by any one of these transactions remain invisible to the other transactions, as is usual for changes made by uncommitted transactions. So the transactions are synchronized with respect to pre-existing data, but act normally for changes they make themselves.

You export snapshots using the [pg_export_snapshot() function](https://www.postgresql.org/docs/15/functions-admin.html#FUNCTIONS-SNAPSHOT-SYNCHRONIZATION), and import them using the SET TRANSACTION command.

This feature is currently {{<tags/feature/tp idea="1161">}} and to use it you must first set the `ysql_enable_pg_export_snapshot` YB-TServer flag to true.

## Export a snapshot

You export a snapshot using PostgreSQL-compatible syntax.

```sql
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT pg_export_snapshot();
```

This produces the following output:

```output
                        pg_export_snapshot
-------------------------------------------------------------------
 6972261563b7411e9f089cc10e259a0c-b1fd4d6c94479680854623cd51c56bec
(1 row)
```

The returned value (`6972261563b7411e9f089cc10e259a0c-b1fd4d6c94479680854623cd51c56bec` in this case) is the snapshot ID, which can be used to import this snapshot.

## Import a snapshot

You can import a snapshot exported using pg_export_snapshot() using the following commands:

```sql
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SET TRANSACTION SNAPSHOT '<snapshot_id>';
```

This ensures that the transaction sees the exact same view of the database as the exporting transaction.

Note that a snapshot ID becomes invalid as soon as the exporting transaction ends. Therefore, a snapshot can only be set if the exporting transaction is still active.

## Example

{{% explore-setup-single-new %}}

The following example demonstrates how to use snapshot export and import across two sessions to maintain a consistent view of the data.

**Session 1**

Start a session and create a table and add data:

```sql
CREATE TABLE test(col INT);
INSERT INTO test VALUES (1), (2);
```

Begin a transaction in session #1 with the REPEATABLE READ isolation level and export the snapshot.

```sql
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT pg_export_snapshot();
```

```output
                        pg_export_snapshot
-------------------------------------------------------------------
 6972261563b7411e9f089cc10e259a0c-fb4af3800930a0ad184858353be37352
(1 row)
```

**Session 2**

Start a second session, and insert a new row into the table:

```sql
INSERT INTO test VALUES (3);
```

Begin a transaction in session #2 with the REPEATABLE READ isolation level, import the previously exported snapshot, and query the table.

```sql
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SET TRANSACTION SNAPSHOT '6972261563b7411e9f089cc10e259a0c-fb4af3800930a0ad184858353be37352';
SELECT * FROM test;
```

```output
 col
-----
   1
   2
(2 rows)
```

Because this transaction is using the exported snapshot, it does not see the row inserted previously in session #2.

## Limitation

Currently, exporting and setting a snapshot can only be performed in a transaction using the REPEATABLE READ isolation level (issues [#24161](https://github.com/yugabyte/yugabyte-db/issues/24161) and [#24162](https://github.com/yugabyte/yugabyte-db/issues/24162)).
