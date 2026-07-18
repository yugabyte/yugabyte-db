--
-- TABLE_REWRITE
--

-- Drop table for Test 1: ADD PRIMARY KEY.
DROP TABLE rewrite_test_add_pk;

-- Drop table for Test 2: DROP PRIMARY KEY.
DROP TABLE rewrite_test_drop_pk;

-- Drop table for Test 3: ADD COLUMN with PRIMARY KEY.
DROP TABLE rewrite_test_add_col_pk;

-- Drop table for Test 4: Add COLUMN with DEFAULT (volatile).
DROP TABLE rewrite_test_add_col_default;

-- Drop table for Test 5: ALTER COLUMN TYPE.
DROP TABLE rewrite_test_alter_col_type;

-- Drop table for Test 6: Table rewrite operations on a partitioned table.
DROP TABLE rewrite_test_partitioned;

-- Drop table for Test 8: Table rewrite operations on a table using legacy inheritance.
DROP TABLE rewrite_test_inherited_parent CASCADE;
