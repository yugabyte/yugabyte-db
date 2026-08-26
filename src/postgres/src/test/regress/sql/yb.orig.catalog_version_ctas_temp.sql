-- Test that CREATE TEMP TABLE AS SELECT does not increment catalog version
\set template1_db_oid 1
\set db_oid 'CASE WHEN (select count(*) from pg_yb_catalog_version) = 1 THEN :template1_db_oid ELSE (SELECT oid FROM pg_database WHERE datname = \'yugabyte\') END'

SELECT pg_sleep(2);
\set display_catalog_version 'SELECT current_version - last_breaking_version >= 0 AS valid FROM pg_yb_catalog_version WHERE db_oid = :db_oid'

-- Store the baseline version
CREATE TEMP TABLE baseline_version AS SELECT current_version FROM pg_yb_catalog_version WHERE db_oid = :db_oid;

-- CREATE TEMP TABLE AS SELECT should not increment catalog version
CREATE TEMP TABLE temp_ctas AS SELECT 1 AS id;

-- Assert that it didn't change
SELECT current_version = (SELECT current_version FROM baseline_version) AS ctas_temp_no_increment FROM pg_yb_catalog_version WHERE db_oid = :db_oid;

-- CREATE TABLE AS SELECT should increment catalog version
CREATE TABLE reg_ctas AS SELECT 1 AS id;

-- Assert that it DID change
SELECT current_version > (SELECT current_version FROM baseline_version) AS ctas_reg_increment FROM pg_yb_catalog_version WHERE db_oid = :db_oid;

DROP TABLE reg_ctas;
