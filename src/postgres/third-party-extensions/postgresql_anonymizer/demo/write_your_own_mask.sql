BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.load();

CREATE TABLE company (
  business_name TEXT,
  info JSONB
);

INSERT INTO company
VALUES ( 'Soylent Green',
'{"employees":[
    {"firstName":"John", "lastName":"Doe"},
    {"firstName":"Anna", "lastName":"Smith"},
    {"firstName":"Peter", "lastName":"Jones"}
]}');

SELECT jsonb_pretty(info) FROM company WHERE business_name = 'Soylent Green';

CREATE FUNCTION remove_last_name(j JSONB)
RETURNS JSONB
VOLATILE
LANGUAGE SQL
AS $func$
SELECT
  json_build_object(
    'employees' ,
    array_agg(
      jsonb_set(e ,'{lastName}', to_jsonb(anon.fake_last_name()))
    )
  )::JSONB
FROM jsonb_array_elements( j->'employees') e
$func$;

SELECT remove_last_name(info) FROM company;

SECURITY LABEL FOR anon ON COLUMN company.info
IS 'MASKED WITH FUNCTION remove_last_name(info)';

SELECT anon.anonymize_table('company');

SELECT jsonb_pretty(info) FROM company WHERE business_name = 'Soylent Green';

ROLLBACK;
