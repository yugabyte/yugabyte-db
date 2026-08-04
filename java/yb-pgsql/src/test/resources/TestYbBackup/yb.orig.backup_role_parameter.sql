\set ON_ERROR_STOP on

CREATE TABLE test_table(id INT PRIMARY KEY);
INSERT INTO test_table (id) VALUES (1), (2), (3);

SHOW log_min_messages;

CREATE ROLE developer LOGIN NOINHERIT;
ALTER ROLE developer SET log_min_messages TO 'DEBUG1';

CREATE ROLE admin LOGIN NOINHERIT;
ALTER ROLE admin IN DATABASE yugabyte SET log_min_messages TO 'LOG';
