BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.is_initialized() IS FALSE;

-- 2 event triggers are defined
SELECT evtname IN ('anon_trg_check_trusted_schemas','anon_trg_mask_update')
FROM pg_event_trigger;

-- basic usage
SELECT anon.init();
SELECT anon.is_initialized();
SELECT anon.reset();

-- returns a WARNING and FALSE
SELECT anon.init('./does/not/exists/cd2ks3s/') IS FALSE;
SELECT anon.is_initialized() IS FALSE;

-- load alternate data dir
\! mkdir -p /tmp/tmp_anon_alternate_data
\! cp -pr data/*.csv /tmp/tmp_anon_alternate_data
\! cp -pr data/fr_FR/fake/*.csv /tmp/tmp_anon_alternate_data
SELECT anon.init('/tmp/tmp_anon_alternate_data');

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

ROLLBACK;
