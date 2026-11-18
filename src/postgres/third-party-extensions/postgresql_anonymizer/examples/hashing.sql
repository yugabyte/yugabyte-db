--
-- This is a demo of the hashing function
-- We will try to pseudonymize a natural foreign key
--

BEGIN;

-- We have a simplistic customer table
CREATE TABLE customer (
  id SERIAL,
  name TEXT,
  phone_number TEXT UNIQUE NOT NULL
);

INSERT INTO customer VALUES
(2046,'Omar Little','410-719-9009'),
(8123,'Russell Bell','410-617-7308'),
(3456,'Avon Barksdale','410-385-2983');

-- We have a log of their calls to our customer service hotline
CREATE TABLE hotline_call_history (
  id SERIAL,
  fk_phone_number TEXT REFERENCES customer(phone_number) DEFERRABLE,
  call_start_time TIMESTAMP,
  call_end_time TIMESTAMP
);

INSERT INTO hotline_call_history VALUES
(834,'410-617-7308','2004-05-17 09:41:01','2004-05-17 09:44:24'),
(835,'410-385-2983','2004-05-17 11:22:55','2004-05-17 11:34:18'),
(839,'410-719-9009','2004-05-18 16:02:03','2004-05-18 16:22:56'),
(878,'410-385-2983','2004-05-20 13:13:34','2004-05-18 13:51:00');

-- We can get a details view of the calls like this
SELECT
  c.id    AS customer_id,
  c.name  AS customer_name,
  h.call_start_time,
  h.call_end_time
FROM
  hotline_call_history h
  JOIN customer c ON h.fk_phone_number = c.phone_number
WHERE
  extract(year from h.call_start_time) = 2004
ORDER BY h.call_start_time;

-- Now let's pseudonymize this !
CREATE EXTENSION IF NOT EXISTS anon CASCADE;

-- Init
SELECT anon.load();
SELECT anon.set_secret_salt('4ag3b803de6180361');
SELECT anon.set_secret_algorithm('md5');

-- Masking Rules
SECURITY LABEL FOR anon ON COLUMN customer.name
IS 'MASKED WITH FUNCTION concat(  anon.pseudo_first_name(name),
                                  $$ $$,
                                  anon.pseudo_last_name(name))';

SECURITY LABEL FOR anon ON COLUMN customer.phone_number
IS 'MASKED WITH FUNCTION anon.hash(phone_number)';

SECURITY LABEL FOR anon ON COLUMN hotline_call_history.fk_phone_number
IS 'MASKED WITH FUNCTION anon.hash(fk_phone_number)';


-- Apply the masking rules
SET CONSTRAINTS ALL DEFERRED;
SELECT anon.anonymize_database();

-- Launch the same request
SELECT
  c.id    AS customer_id,
  c.name  AS customer_name,
  h.call_start_time,
  h.call_end_time
FROM
  hotline_call_history h
  JOIN customer c ON h.fk_phone_number = c.phone_number
WHERE
  extract(year from h.call_start_time) = 2004
ORDER BY h.call_start_time;

--
ROLLBACK;
