# subtrans_infos

This extension allows you to get detailed subtransaction information for any PostgreSQL transaction ID, including:

- Transaction status (in progress, committed, aborted)
- Parent transaction ID
- Top-level parent transaction ID  
- Subtransaction nesting level
- Commit timestamp (when available)

The extension provides comprehensive safety checks and handles PostgreSQL's internal transaction management constraints properly.

## Usage

The extension provides a single function:

```sql
subtrans_infos(xid bigint) 
RETURNS TABLE (
    xid integer,
    status text,
    parent_xid integer,
    top_parent_xid integer,
    sub_level integer,
    commit_timestamp timestamptz
)
```

### Parameters

- `xid`: The transaction ID to analyze (as a bigint)

### Return Columns

- `xid`: The input transaction ID (converted to integer)
- `status`: Transaction status ("in progress", "committed", "aborted")
- `parent_xid`: Direct parent transaction ID (NULL if top-level)
- `top_parent_xid`: Top-level parent transaction ID (NULL if top-level or data unavailable)
- `sub_level`: Subtransaction nesting level (NULL if top-level or data unavailable)
- `commit_timestamp`: Timestamp when transaction was committed (NULL if not committed or unavailable)

## Examples

First, create a test table:

```sql
CREATE TABLE t1 (id int);
```

Start a transaction with savepoints:
```sql
BEGIN;
INSERT INTO t1 VALUES(1); 
SAVEPOINT a;
INSERT INTO t1 VALUES(2); 
SAVEPOINT b;
INSERT INTO t1 VALUES(3);
```

Now analyze the transaction locks and subtransactions:
```sql
SELECT
    pgl.pid,
    pgl.locktype,
    pgl.mode,
    si.xid,
    si.status AS "xid status",
    si.parent_xid,
    si.top_parent_xid,
    si.sub_level,
    si.commit_timestamp
FROM (
    SELECT *
    FROM pg_locks
    WHERE transactionid IS NOT NULL
) pgl
CROSS JOIN LATERAL subtrans_infos(pgl.transactionid::text::bigint) si
ORDER BY si.xid;
```

Expected output (transaction IDs will vary):

```
  pid   |   locktype    |     mode      | xid  | xid status  | parent_xid | top_parent_xid | sub_level | commit_timestamp 
--------+---------------+---------------+------+-------------+------------+----------------+-----------+------------------
 704841 | transactionid | ExclusiveLock | 1647 | in progress |            |                |           | 
 704841 | transactionid | ExclusiveLock | 1648 | in progress |       1647 |           1647 |         1 | 
 704841 | transactionid | ExclusiveLock | 1649 | in progress |       1648 |           1647 |         2 | 
(3 rows)
```

Complete the transaction:
```sql
COMMIT;
```

### Example 2: Individual Transaction Analysis

Query specific transaction IDs:
```sql
-- Check a specific transaction
subtrans_infos=# SELECT * FROM subtrans_infos(1647);

 xid  |  status   | parent_xid | top_parent_xid | sub_level |      commit_timestamp      
------+-----------+------------+----------------+-----------+----------------------------
 1647 | committed |            |                |           | 2025-09-29 10:18:52.488217
(1 row)
```

### Example 3: Analyzing Aborted Subtransactions

```sql
subtrans_infos=# BEGIN;
INSERT INTO t1 VALUES(10);
SAVEPOINT sp1;
INSERT INTO t1 VALUES(20);
SAVEPOINT sp2;
INSERT INTO t1 VALUES(30);
ROLLBACK TO SAVEPOINT sp1;  
BEGIN
INSERT 0 1
SAVEPOINT
INSERT 0 1
SAVEPOINT
INSERT 0 1
ROLLBACK
subtrans_infos=*# SELECT
    si.xid,
    si.status,
    si.parent_xid,
    si.top_parent_xid,
    si.sub_level
FROM pg_locks pgl
CROSS JOIN LATERAL subtrans_infos(pgl.transactionid::text::bigint) si
WHERE pgl.transactionid IS NOT NULL
ORDER BY si.xid;
 xid  |   status    | parent_xid | top_parent_xid | sub_level 
------+-------------+------------+----------------+-----------
 1650 | in progress |            |                |          
(1 row)


subtrans_infos=*# SELECT * FROM subtrans_infos(1651);
 xid  | status  | parent_xid | top_parent_xid | sub_level | commit_timestamp 
------+---------+------------+----------------+-----------+------------------
 1651 | aborted |       1650 |           1650 |         1 | 
(1 row)
```

## Remarks

- `top_parent_xid` and `sub_level` may be NULL when subtransaction data is not available (e.g., for very old transactions)
- Commit timestamps are only available when `track_commit_timestamp` is enabled
- The extension properly handles PostgreSQL's transaction ID wraparound
- All operations are safe and will not crash the database server
