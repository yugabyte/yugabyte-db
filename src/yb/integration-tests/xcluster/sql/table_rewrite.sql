--
-- TABLE_REWRITE
--

-- Test 1: ADD PRIMARY KEY
CREATE TABLE rewrite_test_add_pk (id int, data text);
CREATE INDEX rewrite_test_add_pk_idx ON rewrite_test_add_pk (data);

INSERT INTO rewrite_test_add_pk VALUES (1, 'one'), (2, 'two'), (3, 'three');
-- Table rewrite, add a new column as PRIMARY KEY.
ALTER TABLE rewrite_test_add_pk ADD PRIMARY KEY (id);

\d+ rewrite_test_add_pk
SELECT * FROM rewrite_test_add_pk;


-- Test 2: DROP PRIMARY KEY
CREATE TABLE rewrite_test_drop_pk (id int PRIMARY KEY, data text);
CREATE INDEX rewrite_test_drop_pk_idx ON rewrite_test_drop_pk (data);

INSERT INTO rewrite_test_drop_pk VALUES (1, 'one'), (2, 'two'), (3, 'three');

-- Table rewrite, drop PRIMARY KEY.
ALTER TABLE rewrite_test_drop_pk DROP CONSTRAINT rewrite_test_drop_pk_pkey;

\d+ rewrite_test_drop_pk
SELECT * FROM rewrite_test_drop_pk;


-- Test 3: ADD COLUMN with PRIMARY KEY
CREATE TABLE rewrite_test_add_col_pk (id int, data text);
CREATE INDEX rewrite_test_add_col_pk_idx ON rewrite_test_add_col_pk (data);

-- Table rewrite, add a new column as PRIMARY KEY.
ALTER TABLE rewrite_test_add_col_pk ADD COLUMN new_id int PRIMARY KEY;

INSERT INTO rewrite_test_add_col_pk VALUES (1, 'one', 1), (2, 'two', 2), (3, 'three', 3);

\d+ rewrite_test_add_col_pk
SELECT * FROM rewrite_test_add_col_pk;


-- Test 4: ADD COLUMN with DEFAULT (volatile)
CREATE TABLE rewrite_test_add_col_default (id int PRIMARY KEY, data text);
CREATE INDEX rewrite_test_add_col_default_idx ON rewrite_test_add_col_default (data);

INSERT INTO rewrite_test_add_col_default VALUES (1, 'one'), (2, 'two'), (3, 'three');

-- Table rewrite, add a column with DEFAULT (volatile).
ALTER TABLE rewrite_test_add_col_default ADD COLUMN created_at
        TIMESTAMP DEFAULT clock_timestamp() NOT NULL;

INSERT INTO rewrite_test_add_col_default (id, data) VALUES (4, 'four');

\d+ rewrite_test_add_col_default
SELECT * FROM rewrite_test_add_col_default;


-- Test 5: ALTER COLUMN TYPE is blocked
CREATE TABLE rewrite_test_alter_col_type (id int PRIMARY KEY, data int);
INSERT INTO rewrite_test_alter_col_type VALUES (1, 1), (2, 2), (3, 3);

\d+ rewrite_test_alter_col_type
SELECT * FROM rewrite_test_alter_col_type;


-- Test 6: table rewrite operations on a partitioned table.
CREATE TABLE rewrite_test_partitioned(a int, PRIMARY KEY (a ASC), b text) PARTITION BY RANGE (a);
CREATE TABLE rewrite_test_partitioned_part1 PARTITION OF rewrite_test_partitioned
        FOR VALUES FROM (1) TO (6);
CREATE TABLE rewrite_test_partitioned_part2 PARTITION OF rewrite_test_partitioned
        FOR VALUES FROM (6) TO (11);

INSERT INTO rewrite_test_partitioned VALUES(generate_series(1, 10), 'text');

SELECT * FROM rewrite_test_partitioned;


-- Test 7: test table rewrite on temp table
CREATE TEMP TABLE temp_table_rewrite_test(a int);
CREATE INDEX ON temp_table_rewrite_test(a);

INSERT INTO temp_table_rewrite_test VALUES(1), (2), (3);

ALTER TABLE temp_table_rewrite_test ADD PRIMARY KEY (a ASC);

SELECT * FROM temp_table_rewrite_test ORDER BY a;


-- Test 8: Table rewrite operations on a table using legacy inheritance.
CREATE TABLE rewrite_test_inherited_parent(a int, b text);
CREATE TABLE rewrite_test_inherited_child1(PRIMARY KEY (a ASC)) 
    INHERITS (rewrite_test_inherited_parent);
CREATE INDEX idx_child1_b ON rewrite_test_inherited_child1(b);

INSERT INTO rewrite_test_inherited_child1 VALUES(1, '100');
INSERT INTO rewrite_test_inherited_child1 VALUES(2, '20');
INSERT INTO rewrite_test_inherited_child1 VALUES(3, '5');

ALTER TABLE rewrite_test_inherited_parent 
  ALTER COLUMN b TYPE int USING b::integer;

SELECT * FROM rewrite_test_inherited_child1 WHERE b > 10 ORDER BY b;
