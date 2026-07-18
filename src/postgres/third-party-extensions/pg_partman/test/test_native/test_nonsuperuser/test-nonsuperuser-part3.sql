-- Additional tests: Testing nonsuperuser

-- NOTE: THIS FILE MUST BE RUN AS A SUPERUSER

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(1);

DROP SCHEMA IF EXISTS partman_test CASCADE;

REVOKE ALL ON SCHEMA partman FROM partman_basic;
REVOKE ALL ON ALL FUNCTIONS IN SCHEMA partman FROM partman_basic;
REVOKE ALL ON ALL TABLES IN SCHEMA partman FROM partman_basic;

REVOKE ALL ON SCHEMA partman FROM partman_owner;
REVOKE ALL ON ALL FUNCTIONS IN SCHEMA partman FROM partman_owner;
REVOKE ALL ON ALL TABLES IN SCHEMA partman FROM partman_owner;

DROP ROLE IF EXISTS partman_basic;
DROP ROLE IF EXISTS partman_owner;
DROP ROLE IF EXISTS partman_revoke;

SELECT hasnt_schema('partman_test', 'Check that test schema was dropped');


SELECT * FROM finish();

