BEGIN;

CREATE EXTENSION IF NOT EXISTS anon;


CREATE TABLE phone (
  phone_owner  TEXT,
  phone_number TEXT
);

INSERT INTO phone VALUES
('Omar Little','410-719-9009'),
('Russell Bell','410-617-7308'),
('Avon Barksdale','410-385-2983');

CREATE ROLE jimmy LOGIN;

GRANT USAGE ON SCHEMA public TO jimmy;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO jimmy;

SECURITY LABEL FOR anon ON ROLE jimmy IS 'MASKED';

SECURITY LABEL FOR anon ON COLUMN phone.phone_owner
IS 'MASKED WITH FUNCTION anon.pseudo_last_name(phone_owner) ';

SET anon.transparent_dynamic_masking TO true;

SELECT anon.init();

SET ROLE jimmy;

SAVEPOINT init;

SELECT COUNT(*) = 0
  FROM phone
  WHERE phone_owner = 'Omar Little';

SELECT (SELECT phone_owner FROM phone WHERE phone_number = '410-719-9009')
     = (SELECT phone_owner FROM phone WHERE phone_number = '410-719-9009');

SELECT anon.pseudo_last_name(243535);

ROLLBACK TO init;

SELECT lower(anon.pseudo_last_name(243535));

ROLLBACK TO init;

SELECT coalesce(anon.pseudo_last_name(243535),'');

ROLLBACK TO init;

SELECT * FROM (SELECT coalesce(anon.pseudo_last_name(243535),'')) AS foo;

ROLLBACK TO init;

SELECT WHERE anon.pseudo_last_name(243535) = '';

ROLLBACK TO init;

EXPLAIN SELECT anon.pseudo_last_name(243535);

ROLLBACK TO init;

WITH test AS ( SELECT anon.pseudo_last_name(243535) ) SELECT * FROM test;

ROLLBACK;
