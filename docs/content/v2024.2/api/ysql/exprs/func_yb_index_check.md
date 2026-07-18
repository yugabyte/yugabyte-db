---
title: yb_index_check() function [YSQL]
headerTitle: yb_index_check()
linkTitle: yb_index_check()
description: Checks if the given index is consistent with its base relation.
tags:
  feature: early-access
menu:
  v2024.2_api:
    identifier: api-ysql-exprs-yb_index_check
    parent: api-ysql-exprs
    weight: 9
type: docs
---

`yb_index_check()` is a utility function that checks if an index is consistent with its base relation. It is useful to detect inconsistencies that can creep in due to faulty storage, faulty RAM, data files being overwritten or modified by unrelated software, or hypothetical undiscovered bugs in YugabytedDB.

It performs checks to detect spurious, missing, and inconsistent index rows. It also validates uniqueness on unique indexes.

If executed on a partitioned index, it will recursively execute on all the child partitions. It does not yet support vector and ybgin indexes.

## Function interface

`yb_index_check(index regclass) returns void`

## Examples

Set tables as follows:

```sql
CREATE TABLE abcd(a int primary key, b int, c int, d int);
CREATE INDEX abcd_b_c_d_idx ON abcd (b ASC) INCLUDE (c, d);
CREATE INDEX abcd_b_c_idx ON abcd(b) INCLUDE (c) WHERE d > 50;
CREATE INDEX abcd_expr_expr1_d_idx ON abcd ((2*c) ASC, (2*b) ASC) INCLUDE (d);
INSERT INTO abcd SELECT i, i, i, i FROM generate_series(1, 10) i;
```

Perform consistency check on index `'abcd_b_c_d_idx'`:

```sql
yugabyte=# SELECT yb_index_check('abcd_b_c_d_idx'::regclass);
```

```output
 yb_index_check
----------------

(1 row)
```

Perform consistency check on an index with oid \= 16906:

```sql
yugabyte=# SELECT yb_index_check(16906);
```

```output
 yb_index_check
----------------

(1 row)
```

Perform consistency check on all the indexes of relation `'abcd'` where `pg_table_size() < 1GB`:

```sql
CREATE OR REPLACE FUNCTION check_all_indexes_in_table(table_oid regclass)
RETURNS TABLE(indexname NAME, yb_index_check TEXT)
LANGUAGE plpgsql
AS $$
DECLARE
    indexrelid oid;
BEGIN
    FOR indexrelid, yb_index_check IN
        SELECT pg_index.indexrelid, 'OK'
        FROM pg_index
        WHERE indrelid = table_oid
          AND pg_table_size(pg_index.indexrelid) < 1024 * 1024 * 1024
    LOOP
        PERFORM yb_index_check(indexrelid::regclass);
        indexname := indexrelid::regclass::name;
        RETURN NEXT;
    END LOOP;
END;
$$;

BEGIN;
SET TRANSACTION ISOLATION LEVEL READ COMMITTED;
SELECT * FROM check_all_indexes_in_table('abcd'::regclass);
```

```output
       indexname       | yb_index_check
-----------------------+----------------
 abcd_b_c_d_idx        | OK
 abcd_b_c_idx          | OK
 abcd_expr_expr1_d_idx | OK
(3 rows)
```

```sql
COMMIT;
```

Note:

- This example uses a `READ COMMITTED` transaction. This reduces the possibility of running into `SNAPSHOT TOO OLD` error by picking a new snapshot for each index.

- In YugabyteDB, there is no separate storage for PK indexes. Consequently, `pg_table_size()` returns null for them, they are not included in the above output. Moreover, PK indexes will always be consistent because the base relation itself acts as the PK index.

Perform consistency check on all the indexes in the current database whose  `pg_table_size() < 1GB`:

```sql
CREATE OR REPLACE FUNCTION check_all_indexes_in_db()
RETURNS TABLE(indexname NAME, yb_index_check TEXT)
LANGUAGE plpgsql
AS $$
BEGIN
    FOR indexname, yb_index_check IN
        SELECT cls.relname, 'OK'
        FROM pg_class cls
        JOIN pg_namespace nsp ON nsp.oid = cls.relnamespace
        WHERE cls.relkind = 'i'  -- 'i' = index
          AND nsp.nspname NOT IN ('pg_catalog', 'information_schema')
          AND pg_table_size(cls.oid) < 1024 * 1024 * 1024
        ORDER BY cls.relname
    LOOP
        PERFORM yb_index_check(indexname::regclass);
        RETURN NEXT;
    END LOOP;
END;
$$;

BEGIN;
SET TRANSACTION ISOLATION LEVEL READ COMMITTED;
SELECT * FROM check_all_indexes_in_db();
```

```output
       indexname       | yb_index_check
-----------------------+----------------
 abcd_b_c_d_idx        | OK
 abcd_b_c_idx          | OK
 abcd_expr_expr1_d_idx | OK
(3 rows)
```

```output
COMMIT;
```

## Reporting issues

An error with the `ERRCODE_INDEX_CORRUPTED` error code is thrown if an index consistency issue is encountered. If no issues are found, the function returns void.

## Repairing corruption

There is no general method of repairing problems that `yb_index_check()` detects. It is best to drop and recreate inconsistent indexes.

## Troubleshooting

### Read restart error

`yb_index_check()` is not concerned with read-after-commit-visibility. It picks up a read time (and an associated snapshot) and uses it to scan both the index and base relation. Even if a write that committed before the chosen read time is missing from the snapshot due to clock skew, that's acceptable—its effects will be absent from both the index and the base table scans.

If '[Restart read required error](../../../../architecture/transactions/read-restart-error/)' is encountered while running `yb_index_check()`, set the following parameter and then re-run `yb_index_check()`:

```sql
SET yb_read_after_commit_visibility=relaxed;
```

This error should not surface while running `yb_index_check()` after issue {{<issue 27288>}}.

### Snapshot too old error

Any operation that takes more time than `timestamp_history_retention_interval_sec` (TServer flag with default value of 900) is susceptible to `Snapshot Too Old` error.

If `yb_index_check()` runs into it:

1. Ensure that GUC `yb_bnl_batch_size` is set to 1024 or larger. `yb_index_check()` internally uses batched nested loop join and hence, uses this parameter.
2. If the issue still persists, try increasing the runtime updatable GFlag `timestamp_history_retention_interval_sec`. It is important to reset the flag value after the index check completes. Not doing so will impact the system's performance and resources. `yb_index_check()` on an index with `pg_table_size()` of 3GB took 700 seconds in a single region, multi-AZ 3-node cluster. This can be used as a benchmark to estimate the flag's value.

This error should not surface while running `yb_index_check()` after issue {{<issue 26283>}}.
