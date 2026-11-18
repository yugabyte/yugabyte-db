--
-- This test relies on the following configuration
--
-- ALTER DATABASE contrib_regression
--   SET anon.masking_policies = 'impexp';
--

BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

CREATE ROLE import_anna LOGIN;

CREATE SCHEMA test;
CREATE TABLE test.t1(c int, t text);
CREATE TABLE test.t2(c int, t text);
CREATE TABLE test.t3(c int, t text);
CREATE VIEW test.v AS SELECT * FROM test.t3;

SECURITY LABEL FOR impexp ON ROLE import_anna IS 'MASKED';

SECURITY LABEL FOR impexp ON SCHEMA test IS 'TRUSTED';
SECURITY LABEL FOR impexp ON TABLE test.t1 IS 'MASKED WHEN c > 10';
SECURITY LABEL FOR impexp ON COLUMN test.t1.t IS 'MASKED WITH FUNCTION pg_catalog.md5(t)';
SECURITY LABEL FOR impexp ON COLUMN test.t2.t IS 'MASKED WITH VALUE NULL';

SECURITY LABEL FOR impexp ON VIEW test.v IS 'MASKED WHEN c > 10';
SECURITY LABEL FOR impexp ON COLUMN test.v.t IS 'MASKED WITH FUNCTION pg_catalog.md5(t)';

SECURITY LABEL FOR impexp ON FUNCTION pg_catalog.md5(text) IS 'TRUSTED';

SELECT jsonb_pretty(x) FROM anon.export_roles_rules('impexp') AS F(x);
SELECT jsonb_pretty(x) FROM anon.export_current_database_rules('impexp') AS F(x);

SELECT provider, objtype, count(*)
  FROM anon.user_rules
 WHERE provider = 'impexp'
 GROUP BY ROLLUP (provider, objtype)
 ORDER BY provider, objtype;

SECURITY LABEL FOR impexp ON ROLE import_anna IS NULL;

SECURITY LABEL FOR impexp ON SCHEMA test IS NULL;
SECURITY LABEL FOR impexp ON TABLE test.t1 IS NULL;
SECURITY LABEL FOR impexp ON COLUMN test.t1.t IS NULL;
SECURITY LABEL FOR impexp ON COLUMN test.t2.t IS NULL;

SECURITY LABEL FOR impexp ON VIEW test.v IS NULL;
SECURITY LABEL FOR impexp ON COLUMN test.v.t IS NULL;

SECURITY LABEL FOR impexp ON FUNCTION pg_catalog.md5(text) IS NULL;

SELECT provider, objtype, count(*)
  FROM anon.user_rules
 WHERE provider = 'impexp'
 GROUP BY ROLLUP (provider, objtype)
 ORDER BY provider, objtype;

SELECT * FROM anon.import_roles_rules(
$$
{
    "import_anna": "MASKED"
}
$$, 'impexp'
);

SELECT * FROM anon.import_database_rules(
$$
{
    "schemas": {
        "test": {
            "mask": "TRUSTED",
            "tables": {
                "t1": {
                    "mask": "MASKED WHEN c > 10",
                    "columns": {
                        "t": "MASKED WITH FUNCTION pg_catalog.md5(t)"
                    }
                },
                "t2": {
                    "columns": {
                        "t": "MASKED WITH VALUE NULL"
                    }
                }
            },
            "views": {
                "v": {
                    "mask": "MASKED WHEN c > 10",
                    "columns": {
                        "t": "MASKED WITH FUNCTION pg_catalog.md5(t)"
                    }
                }
            }
        },
        "pg_catalog": {
            "functions": {
                "pg_catalog.md5(text)": "TRUSTED"
            }
        }
    }
}
$$, 'impexp'
);

SELECT provider, objtype, count(*)
  FROM anon.user_rules
 WHERE provider = 'impexp'
 GROUP BY ROLLUP (provider, objtype)
 ORDER BY provider, objtype;

ROLLBACK;

-- Error: Wrong policy
BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;
CREATE ROLE import_anna LOGIN;

SELECT * FROM anon.import_roles_rules(
$$
{
    "import_anna": "MASKED"
}
$$, 'not_a_policy'
);

ROLLBACK;

-- Error: Wrong user name
BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT * FROM anon.import_roles_rules(
$$
{
    "not_a_user": "MASKED"
}
$$, 'impexp'
);

ROLLBACK;

-- Error: Invalid label syntax
BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;
CREATE SCHEMA test;

SELECT * FROM anon.import_database_rules(
$$
{
    "schemas": {
        "test": {
            "mask": "MISTAKE"
        }
    }
}
$$, 'impexp'
);

ROLLBACK;

-- Error: Invalid relkind
BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;
CREATE SCHEMA test;

SELECT * FROM anon.import_database_rules(
$$
{
    "schemas": {
        "test": {
           "car": {
                 "v": {
                    "mask": "MASKED WHEN c > 10",
                    "columns": {
                        "t": "MASKED WITH FUNCTION pg_catalog.md5(t)"
                    }
                }
            }
        }
    }
}
$$, 'impexp'
);

ROLLBACK;

