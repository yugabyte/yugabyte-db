BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

-- Sample Table
CREATE TABLE a (
    i SERIAL,
    d TIMESTAMP,
    x INTEGER,
    t TEXT
);



--
-- random_phone
--

--returns TRUE
SELECT anon.random_phone('11; SELECT 0;') LIKE '11; SELECT 0;%';

--
-- add_noise_on_numeric_column
--

SAVEPOINT noise_1;
SELECT anon.add_noise_on_numeric_column('a; SELECT 1','x',0.5);
ROLLBACK TO noise_1;

SAVEPOINT noise_2;
SELECT anon.add_noise_on_numeric_column('a','x; SELECT 1',0.5);
ROLLBACK TO noise_2;

--
-- add_noise_on_datetime_column
--

SAVEPOINT noise_3;
SELECT anon.add_noise_on_datetime_column('a; SELECT 1','d','2 days');
ROLLBACK TO noise_3;

SAVEPOINT noise_4;
SELECT anon.add_noise_on_datetime_column('a','d; SELECT 1','2 days');
ROLLBACK TO noise_4;

SAVEPOINT noise_5;
SELECT anon.add_noise_on_datetime_column('a','d','2 days; SELECT 1');
ROLLBACK TO noise_5;

--
-- shuffle_column
--

SAVEPOINT shuffle_1;
SELECT anon.shuffle_column('a; SELECT 1','x','i');
ROLLBACK TO shuffle_1;

SAVEPOINT shuffle_2;
SELECT anon.shuffle_column('a','x; SELECT 1','i');
ROLLBACK TO shuffle_2;

SAVEPOINT shuffle_3;
SELECT anon.shuffle_column('a','x','i; SELECT 1');
ROLLBACK TO shuffle_3;

--
-- load
--

-- returns a WARNING and FALSE
SELECT anon.load('base/''; CREATE TABLE inject_via_load(i int);--') IS FALSE;

SELECT COUNT(*) = 0
FROM pg_tables
WHERE tablename='inject_via_load';

--
-- Dynamic Masking
--

SET anon.maskschema TO 'foo; CREATE TABLE inject_via_guc(i int);--';
SELECT anon.start_dynamic_masking();

SELECT COUNT(*) = 0
FROM pg_tables
WHERE tablename='inject_via_guc';

--
-- Masking Rule Syntax
--

SECURITY LABEL FOR anon ON COLUMN a.t
IS 'MASKED WITH VALUE $$foo; CREATE TABLE inject_via_rule(i int);--$$';

SAVEPOINT masking_rule_2;
SECURITY LABEL FOR anon ON COLUMN a.t
IS 'MASKED WITH VALUE NULL as TEXT) as t FROM a;
    CREATE TABLE inject_via_rule(i int);
    SELECT 1--()';
ROLLBACK TO masking_rule_2;

SAVEPOINT masking_rule_3;
SECURITY LABEL FOR anon ON COLUMN a.t
IS 'MASKED WITH VALUE NULL as t FROM a;
    CREATE TABLE inject_via_rule(i int);
    SELECT 1--()';
ROLLBACK TO masking_rule_3;

SAVEPOINT masking_rule_4;
SECURITY LABEL FOR anon ON COLUMN a.t
IS 'MASKED WITH FUNCTION anon.fake_lastname() as TEXT) as t FROM a;
    CREATE TABLE inject_via_rule(i int);
    SELECT 1--()';
ROLLBACK TO masking_rule_4;

SAVEPOINT masking_rule_5;
SECURITY LABEL FOR anon ON COLUMN a.t
IS 'MASKED WITH FUNCTION anon.fake_lastname() as t FROM a;
    CREATE TABLE inject_via_rule(i int);
    SELECT 1--()';
ROLLBACK TO masking_rule_5;


SAVEPOINT masking_rule_6;
SECURITY LABEL FOR anon ON TABLE a
IS 'TABLESAMPLE SYSTEM(10);
    CREATE TABLE inject_via_rule(i int);
    --';
ROLLBACK TO masking_rule_6;
SELECT COUNT(*) = 0
FROM pg_tables
WHERE tablename='inject_via_rule';


--
-- Reading a privileged table
--

CREATE TABLE hashes (a int);

SAVEPOINT masking_rule_7;
SECURITY LABEL FOR anon ON COLUMN hashes.a
IS 'MASKED WITH VALUE 1 as INT) as a,
                      rolname,
                      rolpassword
                FROM pg_catalog.pg_authid --';
ROLLBACK TO masking_rule_7;


SAVEPOINT masking_rule_8;
SECURITY LABEL FOR anon ON COLUMN hashes.a
IS 'MASKED WITH VALUE 1 as a,
                      rolname,
                      rolpassword
                FROM pg_catalog.pg_authid --';
ROLLBACK TO masking_rule_8;

-- CLEAN UP
DROP EXTENSION anon CASCADE;
ROLLBACK;


