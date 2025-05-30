CREATE TABLE yb_row_locking
(
    hk int,
    rk int,
    v1 int,
    v2 int,
    PRIMARY KEY(hk HASH, rk)
);

CREATE OR REPLACE FUNCTION get_lock_status()
RETURNS TABLE(
    locktype text,
    relation text,
    mode text[],
    granted boolean,
    is_explicit boolean,
    hash_cols text[],
    range_cols text[],
    attnum smallint,
    has_column_id bool,
    multiple_rows_locked boolean
) AS $$
BEGIN
    RETURN QUERY
    SELECT l.locktype,
           l.relation::regclass::text,
           l.mode,
           l.granted,
           l.is_explicit,
           l.hash_cols,
           l.range_cols,
           l.attnum,
           CASE WHEN l.column_id IS NOT NULL THEN true ELSE false END,
           l.multiple_rows_locked
    FROM yb_lock_status(null, null) l
    ORDER BY l.relation::regclass::text, l.transaction_id, l.hash_cols NULLS FIRST,
             l.range_cols NULLS FIRST, l.column_id NULLS FIRST;
END;
$$ LANGUAGE plpgsql;

INSERT INTO yb_row_locking VALUES(1,1,1,1);

-- When updating a row in any isolation level it should result in a STRONG_READ (row lock) on the
-- primary key.
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
UPDATE yb_row_locking SET v1 = 2 WHERE hk = 1 AND rk = 1;
SELECT * FROM get_lock_status();
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
UPDATE yb_row_locking SET v1 = 3 WHERE hk = 1 AND rk = 1;
SELECT * FROM get_lock_status();
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
UPDATE yb_row_locking SET v1 = 4 WHERE hk = 1 AND rk = 1;
SELECT * FROM get_lock_status();
COMMIT;

-- When a value isn't updated we should not write locks to the intentdb in REPEATABLE READ and READ
-- COMMITTED.
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
UPDATE yb_row_locking SET v1 = 2 WHERE hk = 2 AND rk = 2;
SELECT * FROM get_lock_status(); -- no locks
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
UPDATE yb_row_locking SET v1 = 3 WHERE hk = 2 AND rk = 2;
SELECT * FROM get_lock_status(); -- no locks
COMMIT;

-- In SERIALIZABLE we need to lock the rows that we have attempted to read even when nothing has
-- been updated
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
UPDATE yb_row_locking SET v1 = 4 WHERE hk = 2 AND rk = 2;
SELECT * FROM get_lock_status(); -- lock
COMMIT;
