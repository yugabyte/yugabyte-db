-- -----------------------------------------------------------------------------
-- SCENARIO 1: Parent exists, Child is new
-- -----------------------------------------------------------------------------
CREATE TABLE fk_parent_exists (id INT PRIMARY KEY);
INSERT INTO fk_parent_exists VALUES (1);

BEGIN;
-- Child is new
CREATE TABLE fk_child_new (
    id INT PRIMARY KEY,
    p_id INT REFERENCES fk_parent_exists(id)
);

-- Should succeed: Parent 1 exists in Regular DB
INSERT INTO fk_child_new VALUES (100, 1);

-- Should fail: Parent 99 does not exist
INSERT INTO fk_child_new VALUES (101, 99);

ROLLBACK;

-- -----------------------------------------------------------------------------
-- SCENARIO 2: Both Parent and Child created in the same Transaction
-- -----------------------------------------------------------------------------
-- This tests if the FK check on 'child' can see the uncommitted 'direct' writes
-- to 'parent'.
BEGIN;

CREATE TABLE fk_parent_txn (id INT PRIMARY KEY);
CREATE TABLE fk_child_txn (
    id INT PRIMARY KEY,
    p_id INT REFERENCES fk_parent_txn(id)
);

-- 1. Insert into parent
INSERT INTO fk_parent_txn VALUES (1);

-- 2. Insert into child
-- This requires the FK check to look into fk_parent_txn's Regular DB entries.
INSERT INTO fk_child_txn VALUES (10, 1);

-- 3. Trigger a failure to ensure both roll back physically
SAVEPOINT sp1;
INSERT INTO fk_child_txn VALUES (11, 99); -- FK Violation
ROLLBACK TO SAVEPOINT sp1;

-- 4. Verify visibility within the transaction
SELECT count(*) FROM fk_child_txn;

COMMIT;

-- -----------------------------------------------------------------------------
-- SCENARIO 3: Circular/Self-Referencing FK in same Transaction
-- -----------------------------------------------------------------------------
BEGIN;

CREATE TABLE fk_self_ref (
    id INT PRIMARY KEY,
    parent_id INT REFERENCES fk_self_ref(id)
);

-- Inserting a row that references itself.
INSERT INTO fk_self_ref VALUES (1, 1);

-- Should fail
INSERT INTO fk_self_ref VALUES (2, 3);

COMMIT;
