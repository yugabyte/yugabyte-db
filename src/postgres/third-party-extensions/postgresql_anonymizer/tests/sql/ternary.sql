BEGIN;
CREATE EXTENSION anon CASCADE;

SELECT anon.init();

SELECT anon.ternary(true,'a'::TEXT,'b'::TEXT) = 'a';
SELECT anon.ternary(true,0,100) = 0;
SELECT anon.ternary(false,0,100) = 100;
SELECT anon.ternary(NULL,0,100) = 100;

CREATE TABLE account (
  id SERIAL,
  login TEXT,
  password TEXT,
  name TEXT
);

INSERT INTO account
VALUES
  ( 1, 'admin', 'not_a_real_password', NULL),
  ( 26879, 'alice', 'alice123', 'Alice')
;

--
-- For practical reason, the admin user should be able to log in the
-- test database. Given that application users laways have an higher id, we
-- can limit the anonymization of the password to a certain category of users.
--
SECURITY LABEL FOR anon ON COLUMN account.password
  IS 'MASKED WITH FUNCTION anon.ternary( id > 1000, NULL::TEXT, password)';
SECURITY LABEL FOR anon ON COLUMN account.name
  IS 'MASKED WITH FUNCTION anon.ternary(name IS NULL, name, anon.fake_first_name())';


SELECT anon.anonymize_database();

SELECT password = 'not_a_real_password' FROM account WHERE id = 1;

SELECT name IS NULL FROM account WHERE id = 1;

SELECT password IS NULL FROM account WHERE id = 26879;

SELECT name != 'Alice' FROM account WHERE id = 26879;

ROLLBACK;
