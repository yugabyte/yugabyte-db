/* YB: workaround for lack of transactional DDL
BEGIN;
*/ -- YB

CREATE TABLE users (
  id SERIAL,
  login TEXT,
  password TEXT,
  admin BOOL
);

INSERT INTO users
VALUES
  (1,'alice', 'adfsqfcksqhdqijsdizjdfiqqlq<iqq', TRUE),
  (2,'bob', 'a_very_bad_password', FALSE)
;

CREATE ROLE claire;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO claire;

CREATE EXTENSION IF NOT EXISTS anon;

-- Mask only the non admin users
SECURITY LABEL FOR anon ON TABLE users IS 'MASKED WHEN admin IS FALSE';

SECURITY LABEL FOR anon ON COLUMN users.login    IS 'MASKED WITH VALUE NULL';
SECURITY LABEL FOR anon ON COLUMN users.password IS 'MASKED WITH VALUE NULL';

SECURITY LABEL FOR anon ON ROLE claire IS 'MASKED';

BEGIN; -- YB: workaround for lack of transactional DDL
SAVEPOINT init;


-- Table level static masking + Selective Masking

SELECT anon.anonymize_table('users');

SELECT bool_and(login IS NULL) FROM users WHERE NOT admin;

SELECT login='alice' FROM users WHERE id=1;

ROLLBACK TO init;

-- Column level static masking + Selective Masking

SELECT anon.anonymize_column('users','login');

SELECT bool_and(login IS NULL) FROM users WHERE NOT admin;

SELECT login='alice' FROM users WHERE id=1;

ROLLBACK TO init;

-- Disable Selective Masking

SECURITY LABEL FOR anon ON TABLE users IS NULL;

SELECT anon.anonymize_table('users');

SELECT login IS NULL FROM users WHERE id=1;

ROLLBACK TO init;
SECURITY LABEL FOR anon ON TABLE users IS 'MASKED WHEN admin IS FALSE'; -- YB: workaround for lack of transactional DDL, the label change escaped the ROLLBACK TO

SET anon.transparent_dynamic_masking TO on;

SET ROLE claire;

SELECT * FROM users;

RESET ROLE;

--
-- Valid rules
--
CREATE FUNCTION public.trusted()
  RETURNS BOOL
AS $$
  SELECT TRUE
$$
  LANGUAGE SQL
;

SECURITY LABEL FOR anon ON FUNCTION public.trusted
  IS 'TRUSTED';

SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN public.trusted()';

SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN NOT public.trusted()';

SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN admin IS TRUE';

SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN admin != FALSE';

SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN admin IS NOT NULL';

SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN NOT admin';

SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN admin';

--
-- Invalid Rules
--
SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN admin IS FALSE AS BOOL ) THEN 1 ELSE 0 END; SELECT sql_injection_101(); CASE WHEN CAST( TRUE';

ROLLBACK TO init;
SECURITY LABEL FOR anon ON TABLE users IS 'MASKED WHEN admin IS FALSE'; -- YB: workaround for lack of transactional DDL, the label change escaped the ROLLBACK TO
DROP FUNCTION public.trusted(); -- YB: workaround for lack of transactional DDL, the function escaped the ROLLBACK TO

CREATE FUNCTION public.untrusted()
  RETURNS BOOL
AS $$
  SELECT TRUE
$$
  LANGUAGE SQL
;

SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN public.unstrusted()';

ROLLBACK TO init;
DROP FUNCTION public.untrusted(); -- YB: workaround for lack of transactional DDL, the function escaped the ROLLBACK TO

SECURITY LABEL FOR anon ON TABLE users
  IS 'MASKED WHEN trusted()';

ROLLBACK TO init;


ROLLBACK;
DROP TABLE users CASCADE; -- YB: workaround for lack of transactional DDL
DROP ROLE claire; -- YB: workaround for lack of transactional DDL
DROP EXTENSION anon CASCADE; -- YB: workaround for lack of transactional DDL
