-- ########## TIME HOURLY TESTS ##########
-- Additional tests: Testing nonsuperuser

-- NOTE: THIS FILE MUST BE RUN AS A SUPERUSER

\set ON_ERROR_ROLLBACK 1
\set ON_ERROR_STOP true

SELECT set_config('search_path','partman, public',false);

SELECT plan(1);

CREATE SCHEMA partman_test;
CREATE ROLE partman_basic WITH LOGIN;
CREATE ROLE partman_owner WITH LOGIN;
CREATE ROLE partman_revoke;

GRANT ALL ON ALL FUNCTIONS IN SCHEMA partman TO partman_basic;
GRANT ALL ON ALL TABLES IN SCHEMA partman TO partman_basic;

GRANT ALL ON SCHEMA partman TO partman_basic;
GRANT ALL ON SCHEMA partman_test TO partman_basic;
GRANT ALL ON SCHEMA partman TO partman_owner;
GRANT ALL ON SCHEMA partman_test TO partman_owner;

GRANT ALL ON ALL FUNCTIONS IN SCHEMA partman TO partman_owner;
GRANT ALL ON ALL TABLES IN SCHEMA partman TO partman_owner;


SELECT has_schema('partman_test', 'Check that test schema exits');


SELECT * FROM finish();

