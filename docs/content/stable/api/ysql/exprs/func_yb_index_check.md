---
title: yb_index_check() function [YSQL]
headerTitle: yb_index_check()
linkTitle: yb_index_check()
description: Checks if the given index is consistent with its base relation.
menu:
  stable_api:
    identifier: api-ysql-exprs-yb_index_check
    parent: api-ysql-exprs
    weight: 9
type: docs
---

## Synopsis

`yb_index_check()` is a utility function that checks if an index is consistent with its base relation. It is useful to detect inconsistencies that can creep in due to faulty storage, faulty RAM, data files being overwritten or modified by unrelated software, or hypothetical undiscovered bugs in YugabyteDB.

It performs checks to detect spurious, missing, and inconsistent index rows. It also validates uniqueness on unique indexes.

If executed on a partitioned index, it recursively checks every child partition. It does not yet support vector and ybgin indexes.

By default the function runs in multi-snapshot mode: it splits the scan into batches and takes a fresh snapshot for each batch so that a long-running check does not hit `Snapshot too old`. Pass `single_snapshot_mode => true` to scan the whole index under one snapshot.

By default (`log_num_errors => 0`) the function errors out on the first inconsistency. Pass a positive `log_num_errors` to collect up to that many inconsistencies as result rows and in the server log, then stop. `log_num_errors > 0` requires multi-snapshot mode.

If no inconsistencies are found, the function returns zero rows.

## Function interface

```output
yb_index_check(
    indexrelid oid,
    single_snapshot_mode boolean DEFAULT false,
    log_num_errors integer DEFAULT 0
) RETURNS TABLE(
    tablerelid oid,
    indexrelid oid,
    ybctid bytea,
    table_cols jsonb,
    ybbasectid bytea,
    index_cols jsonb,
    error_category text
)
```

You can pass an index name with a `regclass` cast (`'my_idx'::regclass`); YSQL converts it to `oid`.

| Parameter | Type | Default | Description |
|---|---|---|---|
| `indexrelid` | `oid` | required | OID of the index (or partitioned index) to check. |
| `single_snapshot_mode` | `boolean` | `false` | When `false` (the default), the check runs in multi-snapshot mode. When `true`, the check scans the index and base table under a single snapshot. |
| `log_num_errors` | `integer` | `0` | Maximum number of inconsistencies to report. `0` errors out on the first inconsistency. A positive value logs and returns up to that many inconsistencies, then stops. Must be non-negative. `log_num_errors > 0` is not supported with `single_snapshot_mode => true`. |

`yb_index_check()` is a set-returning function. Use `SELECT * FROM yb_index_check(...)` to return each inconsistency as a row with the columns above. A consistent index returns zero rows.

A `LIMIT` clause on the outer query does not stop the checker early, so pass `log_num_errors` when you want the scan to stop after N findings.

## Snapshot modes

`single_snapshot_mode` selects how `yb_index_check()` pins read time. The default (`false`) is multi-snapshot mode, which uses a sequence of snapshots so a long-running check does not hit `Snapshot too old`.

**Single-snapshot mode** (`single_snapshot_mode => true`) scans the index and base table under one snapshot. Use this when you need every row observed at one read time. It can hit `Snapshot too old` on large indexes, and it rejects `log_num_errors > 0`.

## Row-level security

`yb_index_check()` reads every physical row of the base table. If row-level security is enabled on that table and the caller cannot bypass it, the function fails before scanning.

The check is allowed for superusers, table owners (unless `FORCE ROW LEVEL SECURITY` is set), and roles with `BYPASSRLS`. Tables without RLS are unaffected. On a partitioned index, the same rule applies to each partition's base table.

## Examples

Set tables as follows:

```sql
CREATE TABLE abcd(a int primary key, b int, c int, d int);
CREATE INDEX abcd_b_c_d_idx ON abcd (b ASC) INCLUDE (c, d);
CREATE INDEX abcd_b_c_idx ON abcd(b) INCLUDE (c) WHERE d > 50;
CREATE INDEX abcd_expr_expr1_d_idx ON abcd ((2*c) ASC, (2*b) ASC) INCLUDE (d);
INSERT INTO abcd SELECT i, i, i, i FROM generate_series(1, 10) i;
```

Perform a consistency check on index `'abcd_b_c_d_idx'`:

```sql
yugabyte=# SELECT * FROM yb_index_check('abcd_b_c_d_idx'::regclass);
```

```output
 tablerelid | indexrelid | ybctid | table_cols | ybbasectid | index_cols | error_category
------------+------------+--------+------------+------------+------------+----------------
(0 rows)
```

Zero rows means the index is consistent with its base table.

Perform a consistency check on an index with oid \= 16906:

```sql
yugabyte=# SELECT * FROM yb_index_check(16906);
```

```output
 tablerelid | indexrelid | ybctid | table_cols | ybbasectid | index_cols | error_category
------------+------------+--------+------------+------------+------------+----------------
(0 rows)
```

Report up to 100 inconsistencies without failing on the first one. This requires multi-snapshot mode (`single_snapshot_mode => false`, the default):

```sql
yugabyte=# SELECT * FROM yb_index_check('abcd_b_c_d_idx'::regclass, false, 100);
```

```output
 tablerelid | indexrelid | ybctid | table_cols | ybbasectid | index_cols | error_category
------------+------------+--------+------------+------------+------------+----------------
(0 rows)
```

If the index is inconsistent, each row is one finding. For example:

```sql
yugabyte=# SELECT * FROM yb_index_check('abcd_b_c_d_idx'::regclass, false, 100);
```

```output
 tablerelid | indexrelid |         ybctid         |         table_cols          |       ybbasectid       |        index_cols        |   error_category
------------+------------+------------------------+-----------------------------+------------------------+--------------------------+---------------------
      19968 |      19973 | \x47fca048800000032121 | {"b": 9999, "c": 3, "d": 3} | \x47fca048800000032121 | {"b": 3, "c": 3, "d": 3} | BINARY_KEY_MISMATCH
      19968 |      19973 |                        |                             | \x47121048800000012121 | {"b": 1, "c": 1, "d": 1} | SPURIOUS_ROW
      19968 |      19973 | \x471c99488000000b2121 |                             |                        |                          | MISSING_ROW
(3 rows)
```

`table_cols` and `index_cols` are omitted from the PostgreSQL log; they appear only in the SQL result. `MISSING_ROW` findings do not populate `table_cols` or `index_cols`. The corresponding log for the example above is:

```output
LOG:  inconsistent index row due to binary mismatch of key attribute
DETAIL:  index: 'abcd_b_c_d_idx', ybbasectid: '\x47fca048800000032121', index attnum: 1
LOG:  index contains spurious row
DETAIL:  index: 'abcd_b_c_d_idx', ybbasectid: '\x47121048800000012121'
LOG:  index 'abcd_b_c_d_idx' is missing row corresponding to ybctid '\x471c99488000000b2121'
```

Force single-snapshot mode (all rows under one snapshot):

```sql
yugabyte=# SELECT * FROM yb_index_check('abcd_b_c_d_idx'::regclass, true);
```

```output
 tablerelid | indexrelid | ybctid | table_cols | ybbasectid | index_cols | error_category
------------+------------+--------+------------+------------+------------+----------------
(0 rows)
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

Note:

- In YugabyteDB, there is no separate storage for PK indexes. Consequently, `pg_table_size()` returns null for them, they are not included in the above output. Moreover, PK indexes will always be consistent because the base relation itself acts as the PK index.

Perform consistency check on all the indexes in the current database whose `pg_table_size() < 1GB`:

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

## Reporting issues

When `log_num_errors` is `0` (the default), the first inconsistency raises an error with SQLSTATE `XX002` (`ERRCODE_INDEX_CORRUPTED`) and the check stops. A consistent index returns zero rows.

When `log_num_errors` is greater than `0`, each inconsistency is returned as a result row and written to the server log at `LOG` level (still with `ERRCODE_INDEX_CORRUPTED`). The check stops after that many findings and raises a NOTICE:

```sql
yugabyte=# SELECT * FROM yb_index_check('abcd_b_c_d_idx'::regclass, false, 3);
```

```output
NOTICE:  index checker aborted: maximum reportable errors (3) reached. Consider dropping and recreating the index.
 tablerelid | indexrelid |         ybctid         |         table_cols          |       ybbasectid       |        index_cols        |   error_category
------------+------------+------------------------+-----------------------------+------------------------+--------------------------+---------------------
      19968 |      19973 | \x47fca048800000032121 | {"b": 9999, "c": 3, "d": 3} | \x47fca048800000032121 | {"b": 3, "c": 3, "d": 3} | BINARY_KEY_MISMATCH
      19968 |      19973 |                        |                             | \x47121048800000012121 | {"b": 1, "c": 1, "d": 1} | SPURIOUS_ROW
      19968 |      19973 | \x471c99488000000b2121 |                             |                        |                          | MISSING_ROW
(3 rows)
```

The result columns are:

| Column | Type | Description |
|---|---|---|
| `tablerelid` | `oid` | OID of the base table. On a partitioned index this is the partition that owns the row. |
| `indexrelid` | `oid` | OID of the index (or index partition) that contains the inconsistency. |
| `ybctid` | `bytea` | `ybctid` of the base-table row. Set for missing and corrupted index rows; null for spurious index rows. |
| `table_cols` | `jsonb` | Map of index column names to the corresponding values from the base table. Not populated for `MISSING_ROW`. Not written to the server log. |
| `ybbasectid` | `bytea` | Base-table `ybctid` stored in the index row. Set for spurious and corrupted index rows; null for missing index rows. |
| `index_cols` | `jsonb` | Map of index column names to the values stored in the index row. Not populated for `MISSING_ROW`. Not written to the server log. |
| `error_category` | `text` | Inconsistency class. See the table below. |

`error_category` values:

| Value | Meaning |
|---|---|
| `SPURIOUS_ROW` | The index has a row that does not match any live base-table row. |
| `MISSING_ROW` | The base table has a row that has no matching index row. |
| `BINARY_KEY_MISMATCH` | An index key column is not binary-equal to the corresponding base-table value. |
| `NULL_MISMATCH` | Nullness of an index column does not match the base table. |
| `BINARY_NONKEY_MISMATCH` | A non-key (INCLUDE) column is not binary-equal to the base table, and the type has no equality operator. |
| `SEMANTIC_NONKEY_MISMATCH` | A non-key (INCLUDE) column is not equal to the base table under the type's equality operator. |
| `UNIQUE_SUFFIX_NOT_NULL` | `ybuniqueidxkeysuffix` is not null when it should be. |
| `UNIQUE_SUFFIX_MISMATCH` | `ybuniqueidxkeysuffix` does not match `ybbasectid`. |
| `YBBASECTID_NULL` | The index row has a null `ybbasectid`. |

`ybuniqueidxkeysuffix` is a hidden unique-index column. Unique indexes store the base-table `ybctid` there when nulls-are-distinct semantics apply and at least one key column is NULL; otherwise the column is null.

The base table is the source of truth. The checker reports at most one inconsistency per (base-table row, index row) pair.

## Repairing corruption

There is no general method of repairing problems that `yb_index_check()` detects. It is best to drop and recreate inconsistent indexes.

## Troubleshooting

### Read restart error

`yb_index_check()` is not concerned with read-after-commit-visibility. It picks a read time and uses that snapshot (or, in multi-snapshot mode, a sequence of snapshots) to scan both the index and the base relation. If a write that committed before the chosen read time is missing from the snapshot because of clock skew, that is acceptable: its effects are absent from both scans.

Outside a transaction block, `yb_index_check()` sets `yb_read_after_commit_visibility` to `relaxed` for the duration of the call, so this error should not appear.

If you call `yb_index_check()` from inside a transaction block and hit a [Restart read required](../../../../architecture/transactions/read-restart-error/) error, set the following outside the transaction block and retry:

```sql
SET yb_read_after_commit_visibility = relaxed;
```

This error should not surface for checks run outside a transaction block after issue {{<issue 27288>}}.

### Snapshot too old error

Any operation that takes longer than `timestamp_history_retention_interval_sec` (TServer flag, default 900 seconds) under a single snapshot is susceptible to `Snapshot too old`.

The default multi-snapshot mode is the recommended way to check large indexes and should not hit this error.

If you still see `Snapshot too old`:

1. Confirm you are not forcing single-snapshot mode (`single_snapshot_mode => true`).
2. Ensure that GUC `yb_bnl_batch_size` is 1024 or larger. `yb_index_check()` uses batched nested loop join and honors this parameter.
3. If you must use single-snapshot mode, increase the runtime-updatable GFlag `timestamp_history_retention_interval_sec` for the duration of the check. It is important to reset the flag value after the index check completes. Not doing so will impact the system's performance and resources. `yb_index_check()` on an index with `pg_table_size()` of 3GB took 700 seconds in a single region, multi-AZ 3-node cluster. This can be used as a benchmark to estimate the flag's value.

This error should not surface in the default multi-snapshot mode after issue {{<issue 26283>}}.
