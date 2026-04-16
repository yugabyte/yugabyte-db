CREATE EXTENSION IF NOT EXISTS anon CASCADE;

-- INIT

SELECT anon.start_dynamic_masking();


CREATE TABLE t1 (i int);

\! vacuumlo -v contrib_regression

--  CLEAN

DROP EXTENSION anon CASCADE;
DROP SCHEMA mask CASCADE;
DROP TABLE t1;
