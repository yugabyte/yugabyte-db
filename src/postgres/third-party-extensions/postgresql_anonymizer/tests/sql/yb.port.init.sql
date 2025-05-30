/* YB: workaround for lack of transactional DDL
BEGIN;
*/ -- YB

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.is_initialized() IS FALSE;

-- 2 event triggers are defined
SELECT evtname IN ('anon_trg_check_trusted_schemas','anon_trg_mask_update')
FROM pg_event_trigger;

-- basic usage
BEGIN; -- YB: Workaround for read time error, check #25665
SET yb_non_ddl_txn_for_sys_tables_allowed = true; -- YB: next statement updates pg_seclabel and is not a DDL
SELECT anon.init();
COMMIT; -- YB: Workaround for read time error, check #25665
SELECT anon.is_initialized();
SET yb_enable_alter_table_rewrite = false; -- YB: anon.reset() truncates tables which fails without this
SELECT anon.reset();

-- returns a WARNING and FALSE
SELECT anon.init('./does/not/exists/cd2ks3s/') IS FALSE;
SELECT anon.is_initialized() IS FALSE;

-- load alternate data dir
\! mkdir -p /tmp/tmp_anon_alternate_data
\! cp -pr ../data/*.csv /tmp/tmp_anon_alternate_data # YB: Fix directory for data
\! cp -pr ../data/fr_FR/fake/*.csv /tmp/tmp_anon_alternate_data # YB: Fix directory for data
BEGIN; -- YB: Workaround for read time error, check #25665
SELECT anon.init('/tmp/tmp_anon_alternate_data');
COMMIT; -- YB: Workaround for read time error, check #25665

-- Load bad data
\! echo '1\t too \t many \t tabs' > /tmp/tmp_anon_alternate_data/bad.csv
SELECT anon.load_csv('anon.city','/tmp/tmp_anon_alternate_data/bad.csv') IS FALSE;

\! rm -fr /tmp/tmp_anon_alternate_data

SELECT anon.reset();

-- strict mode
SELECT anon.init(NULL) IS NULL;

-- backward compatibility with v0.6 and below
SELECT anon.load();

-- Returns a NOTICE and TRUE
SELECT anon.init();

SELECT anon.is_initialized() IS TRUE;

SELECT anon.reset();

SELECT anon.is_initialized() IS FALSE;

SELECT anon.start_dynamic_masking( autoload := FALSE );

SELECT anon.is_initialized() IS FALSE;

/* YB: workaround for lack of transactional DDL
ROLLBACK;
*/ -- YB

SELECT anon.stop_dynamic_masking(); -- YB: workaround for lack of transactional DDL
DROP EXTENSION anon CASCADE; -- YB: workaround for lack of transactional DDL
