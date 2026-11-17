\set ON_ERROR_STOP off

\dt

-- Test source DB.
\c yugabyte yugabyte_test
\dt  -- No tables

-- Test roles with the resrored DB.
\c db2 yugabyte_test
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (7);

\c db2 admin
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (99); -- Should fail

\c db2 "CaseSensitiveRole"
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (99); -- Should fail

\c db2 "role_with_a space"
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (99); -- Should fail

\c db2 "Role with spaces"
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (99); -- Should fail

\c db2 "Role with a quote '"
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (99); -- Should fail

\c db2 "Role with 'quotes'"
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (99); -- Should fail

\c db2 "Role with a double quote """
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (99); -- Should fail


\c db2 "Role with double ""quotes"""
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (99); -- Should fail

\c db2 "Role_""_with_""""_different' quotes''"
\dt
SELECT * FROM test_table WHERE id=1;
INSERT INTO test_table (id) VALUES (99); -- Should fail

\c db2 yugabyte_test
SELECT * FROM test_table;
