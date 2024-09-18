--
-- YB tests for views
--

CREATE TABLE test (a int, b int, c int DEFAULT 5);
INSERT INTO test VALUES (generate_series(1, 5), generate_series(1, 5));
CREATE VIEW test_view AS SELECT * FROM test ORDER BY a, b;
SELECT * FROM test_view;

-- Tests for ALTER VIEW.
-- test ALTER VIEW ... ALTER COLUMN ... SET/DROP DEFAULT
ALTER VIEW test_view ALTER COLUMN c SET DEFAULT 10;
INSERT INTO test (a, b) VALUES (6, 6);
INSERT INTO test_view (a, b) VALUES (7, 7);
SELECT * FROM test_view;
ALTER VIEW test_view ALTER COLUMN c DROP DEFAULT;
INSERT INTO test (a, b) VALUES (8, 8);
SELECT * FROM test_view;
ALTER VIEW IF EXISTS non_existent_view ALTER COLUMN c SET DEFAULT 10;
ALTER VIEW IF EXISTS non_existent_view ALTER COLUMN c DROP DEFAULT;
-- test ALTER VIEW ... OWNER TO
CREATE ROLE test_role;
ALTER VIEW test_view OWNER TO test_role;
SELECT * FROM test_view;
ALTER VIEW test_view OWNER TO CURRENT_USER;
SELECT * FROM test_view;
ALTER VIEW IF EXISTS non_existent_view OWNER TO test_role;
-- test ALTER VIEW ... RENAME
ALTER VIEW test_view RENAME TO test_view_renamed;
SELECT * FROM test_view_renamed;
ALTER VIEW IF EXISTS non_existent_view RENAME TO non_existent_view_renamed;
ALTER VIEW test_view_renamed RENAME TO test_view;
-- test ALTER VIEW ... SET SCHEMA
CREATE SCHEMA test_schema;
ALTER VIEW test_view SET SCHEMA test_schema;
SELECT * FROM test_schema.test_view;
ALTER VIEW test_schema.test_view SET SCHEMA public;
SELECT * FROM test_view;
ALTER VIEW IF EXISTS non_existent_view SET SCHEMA test_schema;
