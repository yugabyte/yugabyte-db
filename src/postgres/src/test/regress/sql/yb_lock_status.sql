CREATE TABLE yb_lock_tests
(
    k1 int,
    k2 int,
    r1 int,
    r2 text,
    v1 text,
    v2 text,
    PRIMARY KEY((k1, k2) HASH, r1,r2)
) SPLIT INTO 2 TABLETS;

CREATE UNIQUE INDEX yb_lock_tests_k1_k2 ON yb_lock_tests (k1,k2) SPLIT INTO 2 TABLETS;

CREATE FUNCTION is_between_now_and_clock_timestamp(input_time timestamptz)
RETURNS boolean
AS $$
BEGIN
  RETURN input_time >= now() AND input_time <= clock_timestamp();
END;
$$ LANGUAGE plpgsql;


CREATE
OR REPLACE FUNCTION run_yb_lock_status(input_relation oid, input_transaction_id uuid,
                                              OUT locktype text,
                                              OUT relation text, OUT mode text[], OUT granted boolean,
                                              OUT fastpath boolean, OUT valid_waitstart boolean,
                                              OUT valid_waitend boolean, OUT has_node boolean,
                                              OUT has_tablet_id boolean,
                                              OUT has_transaction_id boolean,
                                              OUT valid_subtransaction_id boolean,
                                              OUT has_status_tablet_id boolean,
                                              OUT is_explicit boolean,
                                              OUT hash_cols text[],
                                              OUT range_cols text[], OUT attnum smallint, OUT column_id integer,
                                              OUT multiple_rows_locked boolean, OUT num_blocking int4)
    RETURNS SETOF record
AS
$$
BEGIN
    RETURN QUERY SELECT l.locktype,
                        l.relation::regclass::text,
                        l.mode,
                        l.granted,
                        l.fastpath,
                        is_between_now_and_clock_timestamp(l.waitstart)                 as valid_waitstart,
                        is_between_now_and_clock_timestamp(l.waitend)                   as valid_waitend,
                        CASE WHEN l.node IS NOT NULL THEN true ELSE false END           as has_node,
                        CASE WHEN l.tablet_id IS NOT NULL THEN true ELSE FALSE END      as has_tablet_id,
                        CASE WHEN l.transaction_id IS NOT NULL THEN true ELSE FALSE END as has_transaction_id,
                        (l.subtransaction_id > 0) as valid_subtransaction_id,
                        CASE WHEN l.status_tablet_id IS NOT NULL THEN true ELSE FALSE END as has_status_tablet_id,
                        l.is_explicit,
                        l.hash_cols,
                        l.range_cols,
                        l.attnum,
                        l.column_id,
                        l.multiple_rows_locked,
                        array_length(l.blocked_by, 1)
                 -- TODO: Add the relation arg when we support querying by relation
                 FROM yb_lock_status(null, input_transaction_id) l
                 WHERE l.relation = input_relation
                 ORDER BY l.relation::regclass::text, l.transaction_id, l.hash_cols NULLS FIRST,
                          l.range_cols NULLS FIRST;
END ;
$$ LANGUAGE plpgsql;

-- Basic queries
SELECT true FROM yb_lock_status(null, null);
SELECT true FROM yb_lock_status('yb_lock_tests'::regclass, null);
SELECT true FROM yb_lock_status('yb_lock_tests'::regclass::int4, null);
SELECT true FROM yb_lock_status(null, 'bogus');
SELECT true FROM yb_lock_status(null, '10000000-2000-3000-1000-400000000000');
SELECT true FROM yb_lock_status('yb_lock_tests'::regclass, '10000000-2000-3000-1000-400000000000');

-- READ COMMITTED
-- Basic insert
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
INSERT INTO yb_lock_tests VALUES (1, 1, 1, 'one', 1, 1);
INSERT INTO yb_lock_tests VALUES (2, 2, 2, 'two', 2, 2);
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
COMMIT;

-- Basic Column Update
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
UPDATE yb_lock_tests SET v1 = 2 WHERE k1 = 1 AND k2 = 1;
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
ABORT;

-- Basic primary key update
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
UPDATE yb_lock_tests SET r1 = 2 WHERE k1 = 1 AND k2 = 1;
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
ABORT;

BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
UPDATE yb_lock_tests SET k2 = 2 WHERE k1 = 1 AND k2 = 1;
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
ABORT;

-- SELECT FOR SHARE
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
SELECT * FROM yb_lock_tests FOR SHARE;
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
COMMIT;

-- SELECT FOR KEY SHARE
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
SELECT * FROM yb_lock_tests FOR KEY SHARE;
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
COMMIT;

-- SELECT FOR UPDATE
BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED;
SELECT * FROM yb_lock_tests FOR UPDATE;
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
ABORT;

-- SERIALIZABLE tests
BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
SELECT * from yb_lock_tests;
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
SELECT * from yb_lock_tests where k1 = 1;
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
SELECT * from yb_lock_tests where k1 = 1 and k2 = 1;
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
SELECT * from yb_lock_tests where k1 = 1 and k2 = 1 and r1 = 1 and r2 = 'one';
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
COMMIT;

-- Foreign key reference
CREATE TABLE fk_reference
(
    k1 int,
    k2 int,
    r1 int,
    r2 text,
    FOREIGN KEY (k1, k2, r1, r2) REFERENCES yb_lock_tests (k1, k2, r1, r2),
    PRIMARY KEY (k1, k2, r1, r2)
);

BEGIN;
INSERT INTO fk_reference VALUES(1,1,1,'one');
SELECT * FROM run_yb_lock_status('yb_lock_tests'::regclass, null);
SELECT * FROM run_yb_lock_status('fk_reference'::regclass, null);
SELECT * FROM run_yb_lock_status('yb_lock_tests_k1_k2'::regclass, null);
COMMIT;

-- When a number of rows are inserted
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
INSERT INTO yb_lock_tests SELECT i, i, i, 'value', i, i from generate_series(10, 20) i;
-- yb_lock_status returns entries from all tablets in the table
-- TODO: Remove WHERE when we support the relation argument
SELECT COUNT(DISTINCT(tablet_id)) FROM yb_lock_status('yb_lock_tests'::regclass, null)
                                  WHERE relation = 'yb_lock_tests'::regclass;
ABORT;

-- Should not see any values
SELECT * FROM run_yb_lock_status(null, null);

-- TODO: Add support for colocated tables