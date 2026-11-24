BEGIN;

CREATE EXTENSION anon;

SET anon.transparent_dynamic_masking TO TRUE;

CREATE TABLE t AS SELECT n FROM generate_series(1,100) n;

SECURITY LABEL FOR anon ON COLUMN t.n
  IS 'masked with VALUE 0';

CREATE ROLE dumper LOGIN;

security label for anon on role dumper
  is 'masked';

GRANT SELECT ON TABLE t TO dumper;

SET ROLE dumper;

SELECT n = 0
FROM t
ORDER BY n DESC
LIMIT 1;

PREPARE max_value AS
  SELECT n
  FROM t
  ORDER BY n DESC
  LIMIT 1
;

EXECUTE max_value;

DEALLOCATE PREPARE max_value;

ROLLBACK;
