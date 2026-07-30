CALL TEST_reset();
SELECT yb_data FROM TEST_filtered_ddl_queue();
SELECT yb_xcluster_ddl_replication.get_replication_role();

-- Test 1: ADD and DROP PRIMARY KEY
CREATE TABLE base_table(a int, b int);
CREATE INDEX idx ON base_table(b ASC);
INSERT INTO base_table SELECT i, i%2 FROM generate_series(1, 100) as i;

-- triggers table rewrite
ALTER TABLE base_table ADD PRIMARY KEY (a ASC);

-- verify the table rewrite are captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

-- triggers table rewrite
ALTER TABLE base_table DROP CONSTRAINT base_table_pkey;

-- verify the table rewrite are captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

DROP TABLE base_table;

-- Test 2: ADD COLUMN with PRIMARY KEY
CALL TEST_reset();
CREATE TABLE base_table2(a int, b int);
CREATE INDEX idx2 ON base_table2(b ASC);

-- triggers table rewrite
ALTER TABLE base_table2 ADD COLUMN c int PRIMARY KEY;

-- verify the table rewrite are captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

DROP TABLE base_table2;

-- Test 3: ADD COLUMN with DEFAULT volatile expression
CALL TEST_reset();
CREATE TABLE base_table3(a int, b int);
CREATE INDEX idx3 ON base_table3(b ASC);
INSERT INTO base_table3 SELECT i, i%2 FROM generate_series(1, 100) as i;

-- triggers table rewrite
ALTER TABLE base_table3 ADD COLUMN created_at TIMESTAMP DEFAULT clock_timestamp() NOT NULL;

-- verify the table rewrite are captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

DROP TABLE base_table3;

-- Test 4: ADD COLUMN GENERATED ALWAYS AS IDENTITY
CALL TEST_reset();
CREATE TABLE base_table4(a int, b int);
CREATE INDEX idx4 ON base_table4(b ASC);
INSERT INTO base_table4 SELECT i, i%2 FROM generate_series(1, 100) as i;

-- triggers table rewrite
ALTER TABLE base_table4 ADD COLUMN new_id INT GENERATED ALWAYS AS IDENTITY;

-- verify the table rewrite are captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

DROP TABLE base_table4;

-- Test 5: ALTER COLUMN TYPE on indexed column
CALL TEST_reset();
CREATE TABLE base_table5(a int, b int);
CREATE INDEX idx5 ON base_table5(b ASC);
INSERT INTO base_table5 SELECT i, i%2 FROM generate_series(1, 100) as i;

-- triggers table rewrite
ALTER TABLE base_table5 ALTER COLUMN b TYPE float USING(random());

-- verify the table rewrite are captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

DROP TABLE base_table5;

-- Test 6: ALTER COLUMN TYPE on non-indexed column
CALL TEST_reset();
CREATE TABLE base_table6(a int, b int);
CREATE INDEX idx6 ON base_table6(b ASC);
INSERT INTO base_table6 SELECT i, i%2 FROM generate_series(1, 100) as i;

-- triggers table rewrite
ALTER TABLE base_table6 ALTER COLUMN a TYPE float USING(random());

-- verify the table rewrite are captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

DROP TABLE base_table6;

-- Test 7: ALTER COLUMN TYPE with dependent index (no table rewrite and reindex, index rebuild only)
-- Test 7a: varchar to text
CALL TEST_reset();
CREATE TABLE t1(a varchar);
INSERT INTO t1(a) SELECT 'row_' || i FROM generate_series(1, 100) as i;
CREATE INDEX idx1 ON t1(a);

-- trigger index rebuild
ALTER TABLE t1 ALTER COLUMN a TYPE text;

-- Verify the index recreation are captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

-- Test 7b: changing collation
CALL TEST_reset();
CREATE TABLE t2(a text COLLATE "C");
INSERT INTO t2(a) SELECT 'row_' || i FROM generate_series(1, 50) as i;
CREATE INDEX idx2 ON t2(a);

-- trigger index rebuild
ALTER TABLE t2 ALTER COLUMN a TYPE text COLLATE "en_US";

-- Verify the index recreation are captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

DROP TABLE t1;
DROP TABLE t2;

-- Test 8: multiple table rewrites with manual replication flag
CALL TEST_reset();
CREATE TABLE manual_base(a int, b int);
INSERT INTO manual_base SELECT i, i%2 FROM generate_series(1, 50) as i;

-- Test with manual replication flag enabled
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 1;
ALTER TABLE manual_base ADD PRIMARY KEY (a ASC);
SET yb_xcluster_ddl_replication.enable_manual_ddl_replication = 0;

-- Verify manual replication flag is captured
SELECT yb_data FROM TEST_filtered_ddl_queue() ORDER BY ddl_end_time;
SELECT yb_data FROM yb_xcluster_ddl_replication.replicated_ddls ORDER BY ddl_end_time;

DROP TABLE manual_base;

