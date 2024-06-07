CREATE EXTENSION pg_stat_statements;

--
-- simple and compound statements
--
SET pg_stat_statements.track_utility = TRUE;
SET pg_stat_statements.track_planning = TRUE;

--
-- create / alter user
--
SELECT pg_stat_statements_reset();
CREATE USER foo PASSWORD 'fooooooo';
ALTER USER foo PASSWORD 'foo2';
CREATE ROLE fizzbuzz PASSWORD 'barrr';
ALTER ROLE fizzbuzz PASSWORD 'barrr2';
-- YB: TODO(devansh): change output when re-enabling password redaction (see DB-11332)
SELECT query, calls, rows FROM pg_stat_statements ORDER BY query COLLATE "C";
DROP USER foo;
DROP ROLE fizzbuzz;

DROP EXTENSION pg_stat_statements;
