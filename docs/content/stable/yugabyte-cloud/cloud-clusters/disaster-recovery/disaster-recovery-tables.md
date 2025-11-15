---
title: Manage tables and indexes for disaster recovery in Aeon
headerTitle: Manage tables and indexes in DR
linkTitle: Tables and indexes
description: Manage tables and indexes in clusters with disaster recovery in Aeon
headContent: Add and remove tables and indexes in clusters with disaster recovery
menu:
  stable_yugabyte-cloud:
    parent: disaster-recovery-aeon
    identifier: disaster-recovery-tables-aeon
    weight: 50
type: docs
---

When making DDL changes to databases in replication for disaster recovery (DR) (such as creating, altering, or dropping tables or partitions), you must perform the changes at the SQL level on both the Source and Target.

For each DDL statement:

1. Execute the DDL on the Source, waiting for it to complete.
1. Execute the DDL on the Target, waiting for it to complete.

After both steps are complete, YugabyteDB Aeon should reflect any added/removed tables in the Tables listing for this DR configuration.

In addition, keep in mind the following:

- If you are using Colocated tables, you CREATE TABLE on Source, then CREATE TABLE on Target making sure that you force the Colocation ID to be identical to that on Source.
- If you try to make a DDL change on Source and it fails, you must also make the same attempt on Target and get the same failure.
- TRUNCATE TABLE is not supported. To truncate a table, pause replication, truncate the table on both primary and standby, and resume replication.

Use the following guidance when managing tables and indexes in clusters with DR configured.

## Tables

All DDL operations must first be executed on the Source and then manually applied to the Target in the same order. For example, if you create a table on the Source, you must also create the same table on the Target before executing any subsequent DDL commands on the Source.

For example, if you add a table on the Source as follows:

```sql
CREATE TABLE users (id UUID PRIMARY KEY, name TEXT);
```

Then, on the Target, you would apply the same command:

```sql
CREATE TABLE users (id UUID PRIMARY KEY, name TEXT);
```

Next, if you add a column on the Source:

```sql
ALTER TABLE users ADD COLUMN email TEXT;
```

You must repeat the same operation on the Target before issuing any further DDLs on the Source:

```sql
ALTER TABLE users ADD COLUMN email TEXT;
```

This ensures DDL consistency between the Source and Target.

## Indexes

### Add an index to DR

Indexes are automatically added to replication in an atomic fashion after you create the indexes separately on Source and Target. You don't need to stop the writes on the Source.

CREATE INDEX may kill some in-flight transactions. This is a temporary error. Retry any failed transactions.

Add indexes to replication in the following sequence:

1. Create an index on the Source.

1. Wait for index backfill to finish.

1. Create the same index on the Target.

1. Wait for index backfill to finish.

    For instructions on monitoring backfill, refer to [Create indexes and track the progress](../../../../explore/ysql-language-features/indexes-constraints/index-backfill/).

### Remove an index from DR

When an index is dropped it is automatically removed from DR.

Remove indexes from replication in the following sequence:

1. Drop the index on the Target.

1. Drop the index on the Source.
