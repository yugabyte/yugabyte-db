BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE TABLE people (
  name TEXT
);

COMMENT ON COLUMN people.name
IS 'MASKED WITH FUNCTION anon.fake_lastname();';

CREATE TABLE "T2" (
  rn SERIAL,
  "IBAN" TEXT,
  COMPANY TEXT
);

INSERT INTO "T2"
VALUES (1991,'12345677890','Cyberdyne Systems');

-- New syntax
SECURITY LABEL FOR anon
ON COLUMN  "T2"."IBAN"
IS 'MASKED WITH FUNCTION anon.random_iban()';

SECURITY LABEL FOR anon
ON COLUMN "T2".COMPANY
IS 'MASKED WITH FUNCTION anon.random_company() jenfk snvi  jdvjs';


WITH const AS (
    SELECT
        '%MASKED +WITH +FUNCTION +#"%#(%#)#"%'::TEXT AS pattern_mask_column_function,
        '%MASKED +WITH +CONSTANT +#"%#(%#)#"%'::TEXT AS pattern_mask_column_constant
)
SELECT
  sl.objoid AS attrelid,
  sl.objsubid  AS attnum,
  c.relname,
  a.attname,
  pg_catalog.format_type(a.atttypid, a.atttypmod),
  sl.label AS col_description,
  substring(sl.label from k.pattern_mask_column_function for '#')  AS masking_function,
  substring(sl.label from k.pattern_mask_column_constant for '#')  AS masking_constant,
  100 AS priority -- high priority for the security label syntax
FROM const k,
     pg_catalog.pg_seclabel sl
JOIN pg_catalog.pg_class c ON sl.classoid = c.tableoid AND sl.objoid = c.oid
JOIN pg_catalog.pg_attribute a ON a.attrelid = c.oid AND sl.objsubid = a.attnum
WHERE a.attnum > 0
--  TODO : Filter out the catalog tables
AND NOT a.attisdropped
AND (   sl.label SIMILAR TO k.pattern_mask_column_function ESCAPE '#'
    OR  sl.label SIMILAR TO k.pattern_mask_column_constant ESCAPE '#'
    )
AND sl.provider = 'anon' -- this is hard-coded in anon.c
;

--SELECT * FROM anon.pg_masking_rules;
--SELECT anon.mask_columns('"T2"'::REGCLASS);
--SELECT anon.mask_filters('"T2"'::REGCLASS);
--SELECT anon.start_dynamic_masking();

ROLLBACK;
