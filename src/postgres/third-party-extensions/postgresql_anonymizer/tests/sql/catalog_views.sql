--
-- This test relies on the following configuration
--
-- ALTER DATABASE contrib_regression
--   SET anon.masking_policies = 'devtests, analytics';
--

BEGIN;

CREATE EXTENSION anon;

SECURITY LABEL FOR anon ON FUNCTION pg_catalog.floor(NUMERIC) IS 'TRUSTED';
SECURITY LABEL FOR anon ON FUNCTION pg_catalog.random() IS 'TRUSTED';
SECURITY LABEL FOR anon ON FUNCTION pg_catalog.mod(INT,INT) IS 'TRUSTED';

CREATE SCHEMA nba;

CREATE TABLE nba.player(
  id SERIAL,
  name TEXT,
  total_points INT,
  highest_score INT
);

CREATE ROLE anna LOGIN;
CREATE ROLE devin LOGIN;

SECURITY LABEL FOR devtests ON COLUMN nba.player.name
  IS 'MASKED WITH FUNCTION anon.dummy_name()';

SECURITY LABEL FOR devtests ON COLUMN nba.player.total_points
  IS 'MASKED WITH FUNCTION pg_catalog.floor(pg_catalog.random()*40000)';

SECURITY LABEL FOR devtests ON COLUMN nba.player.highest_score
  IS 'MASKED WITH FUNCTION anon.random_int_between(0,50)';

SECURITY LABEL FOR devtests ON ROLE devin IS 'MASKED';

SECURITY LABEL FOR devtests ON FUNCTION anon.random_int_between IS 'TRUSTED';

SECURITY LABEL FOR analytics ON COLUMN nba.player.name
  IS 'MASKED WITH VALUE NULL';

SECURITY LABEL FOR analytics ON ROLE anna IS 'MASKED';

SECURITY LABEL FOR analytics ON DATABASE contrib_regression IS 'TABLESAMPLE SYSTEM(33)';

-- CHECKS

WITH counts AS (
  SELECT
    (SELECT count(*) FROM anon.all_rules) AS ar,
    (SELECT count(*) FROM anon.user_rules) AS ur,
    (SELECT count(*) FROM anon.sys_rules) AS sr
)
SELECT ar - ur - sr = 0
FROM counts;

SELECT count(*)=0 FROM anon.sys_rules WHERE objtype NOT IN ( 'function', 'schema' );

SELECT count(*)=1 FROM anon.user_rules WHERE objtype = 'database' AND provider = 'analytics';

SELECT count(*)=2 FROM anon.user_rules WHERE objtype != 'database' AND provider = 'analytics';

SELECT count(*)=4 FROM anon.user_rules WHERE provider = 'devtests';

ROLLBACK;

