--
-- Replica Masking is quite hard to test with pg_regress !
--
-- Here's we choose to setup 2 database on the same instance. This is rather
-- unusual configuration but it allows us to run commands from outside the
-- current contrib_regression database
--
-- When the test fails, you might have to clean up manually the source database
-- with :
--
-- psql contrib_regression -c 'ALTER SUBSCRIPTION contrib_sub DISABLE'
-- psql contrib_regression -c 'ALTER SUBSCRIPTION contrib_sub SET (slot_name = NONE)'
-- psql contrib_regression -c 'DROP SUBSCRIPTION contrib_sub';
--
-- Of course, we can't run this test in a single transaction
--

CREATE ROLE contrib_repli LOGIN REPLICATION;

-------------------------------------------------------------------------------
-- Setup the source database
-------------------------------------------------------------------------------

CREATE DATABASE contrib_regression_source;

\! psql contrib_regression_source -c 'CREATE SCHEMA "MyApp";'

\! psql contrib_regression_source -c 'CREATE TABLE "MyApp".person( id SERIAL PRIMARY KEY, name TEXT, company TEXT );'

\! psql contrib_regression_source -c "INSERT INTO \"MyApp\".person VALUES (1, 'Alice', 'CompanyA');"
\! psql contrib_regression_source -c "INSERT INTO \"MyApp\".person VALUES (2, 'Bob', 'CompanyB');"
\! psql contrib_regression_source -c "INSERT INTO \"MyApp\".person VALUES (3, 'Charlie', 'CompanyC');"
\! psql contrib_regression_source -c "INSERT INTO \"MyApp\".person VALUES (4, 'David', 'CompanyD');"
\! psql contrib_regression_source -c "INSERT INTO \"MyApp\".person VALUES (5, 'Eve', 'CompanyE');"


\! psql  contrib_regression_source -c 'GRANT USAGE  ON SCHEMA "MyApp" TO contrib_repli;'
\! psql  contrib_regression_source -c 'GRANT SELECT ON ALL TABLES IN SCHEMA "MyApp" TO contrib_repli;'


-- Creating a subscription that connects to the same database cluster will only
-- succeed if the replication slot is not created as part of the same command.
-- Otherwise, the CREATE SUBSCRIPTION call will hang forever.
\! psql contrib_regression_source -c "SELECT slot_name FROM pg_create_logical_replication_slot('contrib_slot', 'pgoutput');"

-- In CI, the command above may take a few seconds, leading to hanging jobs
-- We pause for a while to be safe
SELECT pg_sleep(10);

\! psql contrib_regression_source -c 'CREATE PUBLICATION contrib_pub FOR TABLE "MyApp".person';


-------------------------------------------------------------------------------
-- Setup the replica
-------------------------------------------------------------------------------

CREATE EXTENSION anon;

CREATE SCHEMA "MyApp";

CREATE TABLE "MyApp".person (
  id SERIAL PRIMARY KEY,
  name TEXT,
  company TEXT
);

CREATE TABLE "MyApp".phone (
  id SERIAL PRIMARY KEY,
  fk_person_id INT,
  number TEXT
);


GRANT USAGE  ON SCHEMA "MyApp" TO contrib_repli;
GRANT ALL    ON ALL TABLES IN SCHEMA "MyApp" TO contrib_repli;


SECURITY LABEL FOR anon ON COLUMN "MyApp".person.company
  IS 'MASKED WITH FUNCTION pg_catalog.md5(company)';

-- This should fail
SELECT anon.start_replica_masking();

SET anon.replica_masking TO on;

SELECT anon.start_replica_masking();

SELECT COUNT(*)=1
FROM pg_trigger
WHERE tgname LIKE 'tg_anon_replica_%';

-- Replication starts now
CREATE SUBSCRIPTION contrib_sub
CONNECTION 'host=localhost user=contrib_repli password=x dbname=contrib_regression_source'
PUBLICATION contrib_pub
WITH (slot_name='contrib_slot', create_slot=false);

-- Synchronisation may a take a few milliseconds
SELECT pg_sleep(2);

-- The data is replicated
SELECT name = 'Alice' FROM "MyApp".person WHERE id=1;

-- The masking rules are applied
SELECT company = pg_catalog.md5('CompanyA') FROM "MyApp".person WHERE id=1;

-------------------------------------------------------------------------------
-- Add a new masking rule
-------------------------------------------------------------------------------

SECURITY LABEL FOR anon ON COLUMN "MyApp".person.name
  IS 'MASKED WITH FUNCTION anon.dummy_name()';

SELECT anon.refresh_replica_masking();

-------------------------------------------------------------------------------
-- Remove a new masking rule
-------------------------------------------------------------------------------

SECURITY LABEL FOR anon ON COLUMN "MyApp".phone.number
  IS 'MASKED WITH VALUE NULL';

SELECT anon.refresh_replica_masking();

SELECT COUNT(*)=1
FROM pg_trigger
WHERE tgname = 'tg_anon_replica_masking_' || '"MyApp".phone'::REGCLASS::OID;

SECURITY LABEL FOR anon ON COLUMN "MyApp".phone.number
  IS NULL;

SELECT anon.refresh_replica_masking();

SELECT COUNT(*)=0
FROM pg_trigger
WHERE tgname = 'tg_anon_replica_masking_' || '"MyApp".phone'::REGCLASS::OID;

-------------------------------------------------------------------------------
-- Testing INSERT
-------------------------------------------------------------------------------

\! psql contrib_regression_source -c "INSERT INTO \"MyApp\".person(id,name, company) VALUES (999999,'John','CompanyF');"

-- Synchronisation may a take a few milliseconds
SELECT pg_sleep(2);

SELECT name != 'John'
FROM "MyApp".person
WHERE id=999999;

SELECT company=md5('CompanyF')
FROM "MyApp".person
WHERE id=999999;


-------------------------------------------------------------------------------
-- DELETE
-------------------------------------------------------------------------------

\! psql contrib_regression_source -c 'DELETE FROM "MyApp".person WHERE id=1;'

-- Synchronisation may a take a few milliseconds
SELECT pg_sleep(2);

SELECT COUNT(*)=0 FROM "MyApp".person WHERE id=1;

-------------------------------------------------------------------------------
-- UPDATE
-------------------------------------------------------------------------------

\! psql contrib_regression_source -c "UPDATE \"MyApp\".person SET name='Omar' WHERE company='CompanyB';"

-- Synchronisation may a take a few milliseconds
SELECT pg_sleep(2);

SELECT name != 'Omar' AND name != 'Bob'
FROM  "MyApp".person
WHERE id=2;

-------------------------------------------------------------------------------
-- Stop Replica Masking
-------------------------------------------------------------------------------

SELECT anon.stop_replica_masking();

SELECT COUNT(*)=0
FROM pg_trigger
WHERE tgname LIKE 'tg_anon_replica_%';

\! psql contrib_regression_source -c "INSERT INTO \"MyApp\".person(id,name,company) VALUES (88,'Alfred', 'CompanyG');"


-- Synchronisation may a take a few milliseconds
SELECT pg_sleep(2);

SELECT name = 'Alfred' AND company = 'CompanyG'
FROM  "MyApp".person
WHERE id=88;

-- Clean up

DROP SCHEMA "MyApp" CASCADE;

DROP SUBSCRIPTION contrib_sub;

\! psql contrib_regression_source -c 'DROP PUBLICATION contrib_pub;'

DROP DATABASE contrib_regression_source;

DROP ROLE contrib_repli;

DROP EXTENSION anon CASCADE;
