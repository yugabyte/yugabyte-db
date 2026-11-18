BEGIN;

CREATE EXTENSION IF NOT EXISTS anon;

CREATE TABLE "Phone" (
  phone_owner  TEXT,
  phone_number TEXT
);

INSERT INTO "Phone" VALUES
('Omar Little','410-719-9009'),
('Russell Bell','410-617-7308'),
('Avon Barksdale','410-385-2983');

CREATE TABLE calls (
  sender TEXT,
  receiver TEXT,
  start_time TIMESTAMP WITH TIME ZONE,
  stop_time TIMESTAMP WITH TIME ZONE
);

INSERT INTO calls VALUES
('410-617-7308','410-385-2983', '2004-07-08 13:05:33.614284+00', '2024-07-08 13:08:09.046126+00');

CREATE SCHEMA baltimore;

CREATE TABLE baltimore.locations(
  zipcode CHAR(5),
  name TEXT
);

INSERT INTO baltimore.locations VALUES
('21206','Raspeburg'),
('21207','Gwynn Oak'),
('21208','Pikesville'),
('21209','Mt Washington'),
('21210','Roland Park');

CREATE TABLE person (
  id SERIAL,
  name TEXT
);

CREATE TABLE french (
  eat_frogs BOOLEAN
)
INHERITS(person);

INSERT INTO french VALUES
(243535,'Robert Bidochon', True);

CREATE TABLE parisian (
  wear_a_beret BOOLEAN
)
INHERITS(french);

INSERT INTO parisian VALUES
(243536,'Amélie Poulain', False, False);

CREATE ROLE jimmy LOGIN;

GRANT USAGE ON SCHEMA public TO jimmy;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO jimmy;
GRANT USAGE ON SCHEMA baltimore TO jimmy;
GRANT SELECT ON ALL TABLES IN SCHEMA baltimore TO jimmy;

SECURITY LABEL FOR anon ON ROLE jimmy IS 'MASKED';

SECURITY LABEL FOR anon ON COLUMN "Phone".phone_owner
IS 'MASKED WITH VALUE $$CONFIDENTIAL$$ ';

SECURITY LABEL FOR anon ON SCHEMA pg_catalog IS 'TRUSTED';

SECURITY LABEL FOR anon ON COLUMN "Phone".phone_number
IS 'MASKED WITH FUNCTION pg_catalog.substring(pg_catalog.md5(phone_number),0,12)';

SECURITY LABEL FOR anon ON COLUMN calls.sender
IS 'MASKED WITH FUNCTION pg_catalog.substring(pg_catalog.md5(sender),0,12)';

SECURITY LABEL FOR anon ON COLUMN calls.receiver
IS 'MASKED WITH FUNCTION pg_catalog.substring(pg_catalog.md5(receiver),0,12)';

SECURITY LABEL FOR anon ON COLUMN baltimore.locations.zipcode
IS 'MASKED WITH FUNCTION anon.partial(zipcode,2,$$xxx$$,0)';

SECURITY LABEL FOR anon ON COLUMN baltimore.locations.name
IS 'MASKED WITH VALUE NULL';

SECURITY LABEL FOR anon ON COLUMN public.person.name
  IS 'MASKED WITH VALUE NULL';

SET anon.transparent_dynamic_masking TO true;

COPY public."Phone" TO stdout;

SET ROLE jimmy;

COPY public."Phone" TO stdout;

COPY (SELECT * FROM "Phone") TO stdout;

SELECT * FROM "Phone";

SELECT * FROM (SELECT * FROM "Phone") AS a;

SELECT * FROM (SELECT * FROM ( SELECT * FROM "Phone") AS b) AS a;

SELECT * FROM (SELECT * FROM ( SELECT * FROM ( SELECT * FROM "Phone") AS c ) AS b) AS a;

WITH cte AS (SELECT * FROM "Phone") SELECT * FROM cte;

SELECT
  a.phone_owner AS sender,
  b.phone_owner AS receiver,
  c.start_time::DATE
FROM calls c
JOIN "Phone" a ON c.sender = a.phone_number
JOIN "Phone" b ON c.receiver = b.phone_number;

-- Masking rules are applied in different schemas
SELECT  pg_catalog.bool_and(zipcode = '21xxx')
FROM baltimore.locations;

-- type with a typmod, for instance `CHAR(N)` are handled correctly
SELECT pg_catalog.bool_and(pg_catalog.length(zipcode)=5)
FROM baltimore.locations;

SAVEPOINT error_anon_role_is_masked;
EXPLAIN ANALYZE SELECT * FROM "Phone";
ROLLBACK TO error_anon_role_is_masked;

RESET ROLE;
GRANT ALL ON ALL TABLES IN SCHEMA baltimore TO jimmy;
SET ROLE jimmy;

SAVEPOINT before_truncate;
TRUNCATE baltimore.locations;
ROLLBACK TO before_truncate;

-- Bug #452 : check that volatile functions work with transparent dynamic masking
RESET ROLE;

CREATE SCHEMA bug452;
CREATE TABLE bug452.t AS SELECT 0.0 as i;

SECURITY LABEL FOR anon ON COLUMN bug452.t.i
  IS $$ MASKED WITH FUNCTION pg_catalog.random() $$;

GRANT USAGE ON SCHEMA bug452 TO jimmy;
GRANT SELECT ON ALL TABLES IN SCHEMA bug452 TO jimmy;

SET ROLE jimmy;

SELECT i IS NOT NULL FROM bug452.t;

SELECT i != 0 FROM bug452.t;

-- bug #553 : FROM ONLY clause should be respected

-- By default, The SELECT command does follow inheritance
SELECT count(*) = 2 FROM public.person;

-- The masking rule placed upon the `person` table are applied
-- to the data of the child tables
SELECT bool_and(name IS NULL) FROM public.person;

-- The ONLY clause is respected
SELECT count(*) = 0 FROM ONLY public.person;


ROLLBACK;
