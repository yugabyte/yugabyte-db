BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE hundred AS
SELECT generate_series(1,100) AS h;

SELECT anon.get_tablesample_ratio('hundred'::REGCLASS::OID) IS NULL;

SAVEPOINT before_error_invalid_label;
  SECURITY LABEL FOR anon ON TABLE hundred IS 'INVALID LABEL';
ROLLBACK TO before_error_invalid_label;

SAVEPOINT before_error_invalid_label2;
  SECURITY LABEL FOR anon ON DATABASE contrib_regression IS 'INVALID LABEL';
ROLLBACK TO before_error_invalid_label2;

SAVEPOINT before_sql_injection;
  SECURITY LABEL FOR anon ON TABLE hundred
  IS 'TABLESAMPLE BERNOULLI (33); CREATE ROLE bob SUPERUSER;';
ROLLBACK TO before_sql_injection;

SECURITY LABEL FOR anon ON TABLE hundred
IS 'TABLESAMPLE SYSTEM(33)';

SELECT anon.get_tablesample_ratio('hundred'::REGCLASS::OID) IS NOT NULL;

SECURITY LABEL FOR anon ON TABLE hundred IS NULL;

SELECT anon.get_tablesample_ratio('hundred'::REGCLASS::OID) IS NULL;

SECURITY LABEL FOR anon ON DATABASE contrib_regression
IS 'TABLESAMPLE BERNOULLI(50)';

SELECT anon.get_tablesample_ratio('hundred'::REGCLASS::OID) IS NOT NULL;


SECURITY LABEL FOR anon ON COLUMN hundred.h
IS 'MASKED WITH VALUE 0';

SELECT count(*) = 100 FROM hundred;

SELECT anon.start_dynamic_masking();

SELECT count(*) < 100 FROM mask.hundred;

-- This should raise a notice
SELECT anon.anonymize_column('hundred','h');

-- The sampling rule is ignored, the table is NOT sampled
SELECT count(*) = 100 FROM hundred;

SAVEPOINT before_anonymize_database;
SELECT anon.anonymize_database();
SELECT count(*) < 100 FROM hundred;
ROLLBACK TO before_anonymize_database;

SAVEPOINT before_anonymize_table;
SELECT anon.anonymize_table('hundred');
SELECT count(*) < 100 FROM hundred;
ROLLBACK TO before_anonymize_table;

SECURITY LABEL FOR anon ON DATABASE contrib_regression IS NULL;

ROLLBACK;
