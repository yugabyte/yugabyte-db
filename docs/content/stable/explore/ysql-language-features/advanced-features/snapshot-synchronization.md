---
title: Snapshot Synchroinzation
linkTitle: Snapshot Synchroinzation
description: Snapshot Synchroinzation in YSQL
menu:
  stable:
    identifier: advanced-features-snapshot-synchroinzation
    parent: advanced-features
    weight: 800
type: docs
---

This document describes how different transactions can maintain a consistent view of the data.

This feature is currently {{<tags/feature/tp>}} and requires the `ysql_enable_pg_export_snapshot` gFlag to be set to true for activation.

{{% explore-setup-single %}}

## Exporting a snapshot

You can export a snapshot using PostgresSQL compatible syntax.

```sql
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT pg_export_snapshot();
```

The preceding query produces the following output:

```output
                        pg_export_snapshot
-------------------------------------------------------------------
 6972261563b7411e9f089cc10e259a0c-b1fd4d6c94479680854623cd51c56bec
(1 row)
```

The returned value (`6972261563b7411e9f089cc10e259a0c-b1fd4d6c94479680854623cd51c56bec` in this case) is the snapshot identifier, which can be used to import this snapshot.

## Setting/Importing a Snapshot

You can import a snapshot exported using pg_export_snapshot() with the following PostgreSQL-compatible syntax:

```sql
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SET TRANSACTION SNAPSHOT 'snapshot_identifier';
```

This ensures that the transaction sees the exact same view of the database as the exporting transaction.

Note: A snapshot identifier becomes invalid as soon as the exporting transaction ends. Therefore, a snapshot can only be set if the exporting transaction is still active.

## Example

The following example demonstrates how to use snapshot export and import across two sessions to maintain a consistent view of the data.

<table>
  <tr>
   <td style="text-align:center;">session #1</td>
   <td style="text-align:center;">session #2</td>
  </tr>

  <tr>
    <td>

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

</td>
    <td>
    </td>
  </tr>

  <tr>
    <td>
    </td>
    <td>

In session #2, insert a new row into the table:

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

Since this transaction is using the exported snapshot, it does not see the row inserted previously in session #2.

</td>
    <td>
    </td>
  </tr>

</table>

## Limitation

Currently, exporting and setting a snapshot can only be performed within a transaction using the REPEATABLE READ isolation level.
