-- This test cannot be run in a single transaction
-- This test must be run on a database named 'contrib_regression'

CREATE EXTENSION IF NOT EXISTS file_fdw;
CREATE SERVER files FOREIGN DATA WRAPPER file_fdw;
CREATE FOREIGN TABLE passwd (
  login text,
  passwd text,
  uid int,
  gid int,
  username text,
  homedir text,
  shell text)
SERVER files
OPTIONS (filename '/etc/passwd', format 'csv', delimiter ':');

-- STEP 1 : Activate the masking engine
CREATE EXTENSION IF NOT EXISTS anon CASCADE;
SELECT anon.start_dynamic_masking();

-- STEP 2 : Declare a masked user
CREATE ROLE skynet LOGIN SUPERUSER PASSWORD 'x';
SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';

-- STEP 3 : Declare the masking rules
SECURITY LABEL FOR anon ON COLUMN passwd.username
IS 'MASKED WITH FUNCTION anon.fake_last_name()';

-- STEP 4 : Connect with the masked user
\! PGPASSWORD=x psql contrib_regression -U skynet -c "SELECT count(*)=0 FROM passwd WHERE username = 'root';"

-- STOP

SELECT anon.stop_dynamic_masking();

--  CLEAN

DROP EXTENSION anon CASCADE;

REASSIGN OWNED BY skynet TO postgres;
DROP OWNED BY skynet CASCADE;
DROP ROLE skynet;
DROP FOREIGN TABLE passwd;
DROP SERVER files CASCADE;
DROP EXTENSION file_fdw;
