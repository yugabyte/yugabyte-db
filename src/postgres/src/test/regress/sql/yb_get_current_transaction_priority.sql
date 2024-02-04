-- Test functionality of the pg_proc yb_get_current_transaction_priority(). It helps fetch the
-- priority of a distributed transaction in Yugabyte if one has been started. If a distributed
-- transaction hasn't been started, it returns 0.
--
-- The priority space of distributed transactions is uint64_t. The 64 bit range is split into 2
-- priority buckets -
--   1. Normal priority bucket:
--        [yb::kRegularTxnLowerBound, yb::kRegularTxnUpperBound] i.e., 0 to uint32_t_max-1
--   2. High priority bucket:
--        [yb::kHighPriTxnLowerBound, yb::kHighPriTxnUpperBound] i.e., uint32_t_max to uint64_t_max
--
-- All transactions are usually randomly assigned a priority in the first bucket (normal). In case a
-- transaction has the first query which takes a FOR UPDATE/ FOR SHARE/ FOR NO KEY UPDATE explicit
-- row lock using SELECT, it is randomly assigned a priority from the high priority bucket.
--
-- Apart from the above rule, there are two other user configurable session variables that can help
-- control the priority assigned to transaction is a specific session. These are
-- yb_transaction_priority_lower_bound and yb_transaction_priority_upper_bound. These help set
-- lower and upper bounds on the randomly assigned priority a transaction should receive from the
-- respective bucket that applies to it. For ease of use, the bounds are expressed as a float
-- such that the numerical ranges of a bucket are proportionally map to floats from 0-1. Also note
-- that the same floating point bounds apply to both buckets.
--
-- For example, if yb_transaction_priority_lower_bound=0.5 and
-- yb_transaction_priority_upper_bound=0.75:
--   1. a transaction that is assigned a priority from the normal bucket will get one from
--      the 0.5-0.75 marks such that the [yb::kRegularTxnLowerBound, yb::kRegularTxnUpperBound]
--      range proportionally maps to 0-1.
--   2. a transaction that is assigned a priority from the high priority bucket will get one from
--      the 0.5-0.75 marks such that the [yb::kHighPriTxnLowerBound, yb::kHighPriTxnUpperBound]
--      range proportionally maps to 0-1.
--
-- The priority returned by yb_get_current_transaction_priority consists of a pair of two values -
-- 1. A float between 0-1 inclusive with 9 decimal units of precision that such that it
--    proportionally maps to the priority assigned in the range of the priority bucket the
--    transaction belongs in.
-- 2. The bucket in which the transaction's priority lies - Normal or High priority.
--
-- Since, priorities are uniformly randomly assigned between the configured lower and upper
-- bounds based on yb_transaction_priority_lower_bound and yb_transaction_priority_upper_bound, to
-- test reliably, we set the yb_transaction_priority_lower_bound and
-- yb_transaction_priority_upper_bound to be the same. This forces the transaction to pick exactly
-- that priority mark in the bucket that applies.
--
-- NOTE: As an exception, if a transaction is assigned the highest priority possible
-- i.e., kHighPriTxnUpperBound, then a single value "Highest priority transaction" is returned
-- without any float.

SET yb_transaction_priority_lower_bound = 0.4;
SET yb_transaction_priority_upper_bound = 0.4;

CREATE TABLE test (k int primary key, v varchar(100));
INSERT INTO test values (1, '1');

-- (1) Check that transaction priority is 0 until a distributed txn is started.
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT yb_get_current_transaction_priority(); -- 0 since a distributed transaction hasn't started
SELECT * FROM test; -- this is a read-only operation and doesn't start a distributed transaction
SELECT yb_get_current_transaction_priority(); -- still 0
INSERT INTO test VALUES (2, '2'); -- start a distributed txn
SELECT yb_get_current_transaction_priority(); -- non-zero now
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
SELECT yb_get_current_transaction_priority(); -- 0 since a distributed transaction hasn't started
SELECT * FROM test; -- reads start a distributed txn in serializable isolation level
SELECT yb_get_current_transaction_priority();
COMMIT;

-- (2) Showing yb_transaction_priority outside a transaction block
SELECT yb_get_current_transaction_priority();

-- (3) Normal priority
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
INSERT INTO test (k, v) SELECT 3, yb_get_current_transaction_priority(); -- starts a distributed transaction
SELECT yb_get_current_transaction_priority();
INSERT INTO test (k, v) SELECT 4, yb_get_current_transaction_priority();
SELECT * FROM test ORDER BY k;
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
INSERT INTO test (k, v) SELECT 5, yb_get_current_transaction_priority(); -- starts a distributed transaction
SELECT yb_get_current_transaction_priority();
INSERT INTO test (k, v) SELECT 6, yb_get_current_transaction_priority();
SELECT * FROM test ORDER BY k;
COMMIT;

-- (4) High priority
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT * FROM test WHERE k = 1 FOR UPDATE; -- starts a distributed transaction in high pri bucket
SELECT yb_get_current_transaction_priority();
INSERT INTO test (k, v) SELECT 7, yb_get_current_transaction_priority();
SELECT * FROM test ORDER BY k;
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
SELECT * FROM test WHERE k = 1 FOR UPDATE; -- starts a distributed transaction in high pri bucket
SELECT yb_get_current_transaction_priority();
INSERT INTO test (k, v) SELECT 8, yb_get_current_transaction_priority();
SELECT * FROM test ORDER BY k;
COMMIT;

-- (5) Highest priority
SET yb_transaction_priority_upper_bound = 1;
SET yb_transaction_priority_lower_bound = 1;
BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ;
SELECT * FROM test WHERE k = 1 FOR UPDATE;
SELECT yb_get_current_transaction_priority();
COMMIT;

BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE;
SELECT * FROM test WHERE k = 1 FOR UPDATE;
SELECT yb_get_current_transaction_priority();
COMMIT;
