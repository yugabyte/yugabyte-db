-- ----------------------------------------------------------------
-- Establish global statement's properties WRT transactions.
-- ----------------------------------------------------------------

-- Outside transaction.

SELECT hll_set_max_sparse(-1);

SELECT hll_set_max_sparse(256);

SELECT hll_set_defaults(11,5,-1,1);

SELECT hll_set_defaults(10,4,128,0);

-- Inside transaction - should have an affect later.

BEGIN;

SELECT hll_set_max_sparse(-1);

SELECT hll_set_defaults(11,5,-1,1);

COMMIT;

SELECT hll_set_max_sparse(256);

SELECT hll_set_defaults(10,4,128,0);

-- Inside Rolled back transaction, should still have an affect later.

BEGIN;

SELECT hll_set_max_sparse(-1);

SELECT hll_set_defaults(11,5,-1,1);

ROLLBACK;

SELECT hll_set_max_sparse(256);

SELECT hll_set_defaults(10,4,128,0);

