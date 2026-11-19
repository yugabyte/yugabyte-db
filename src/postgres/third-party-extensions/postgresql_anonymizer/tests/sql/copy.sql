BEGIN;

CREATE EXTENSION anon;

CREATE TABLE "Phone" (
  "phone_Owner"  TEXT,
  "phone number" TEXT
);

INSERT INTO "Phone" VALUES
('Omar Little','410-719-9009'),
('Russell Bell','410-617-7308'),
('Avon Barksdale','410-385-2983');

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

SET anon.transparent_dynamic_masking TO true;

CREATE ROLE jimmy LOGIN;

GRANT USAGE ON SCHEMA public TO jimmy;
GRANT SELECT ON ALL TABLES IN SCHEMA public TO jimmy;

SECURITY LABEL FOR anon ON ROLE jimmy IS 'MASKED';

SECURITY LABEL FOR anon ON COLUMN "Phone"."phone_Owner"
IS 'MASKED WITH VALUE $$CONFIDENTIAL$$ ';

SECURITY LABEL FOR anon ON SCHEMA pg_catalog IS 'TRUSTED';

SECURITY LABEL FOR anon ON COLUMN "Phone"."phone number"
IS 'MASKED WITH FUNCTION pg_catalog.substring(pg_catalog.md5("phone number"),0,12)';

SET anon.transparent_dynamic_masking TO true;

COPY public."Phone" TO stdout;

SET ROLE jimmy;

COPY public."Phone" TO stdout;

COPY public."Phone" ("phone_Owner") TO stdout;

COPY public."Phone" ("phone number") TO stdout;

COPY public."Phone" ("phone number", "phone_Owner") TO stdout;

COPY (SELECT * FROM "Phone") TO stdout;

-- Testing inheritance
-- the COPY command does not follow the inheritance

COPY public.person TO stdout;

RESET ROLE;

SECURITY LABEL FOR anon ON COLUMN public.person.name
  IS 'MASKED WITH VALUE NULL';

SET ROLE jimmy;

COPY public.person TO stdout;

--
-- Issue #634
--
RESET ROLE;

CREATE TABLE "Super-Weird Column Names !!!" AS
SELECT
  'DONT' AS "col with  multiple   spaces",
  'DO' AS " leading_space",
  'THIS' AS "trailing_space ",
  'AT' AS "123abc!!!!",
  'HOME' AS "col""with""quotes"
;

SECURITY LABEL FOR anon ON COLUMN "Super-Weird Column Names !!!"."col with  multiple   spaces"
  IS 'MASKED WITH VALUE NULL';
SECURITY LABEL FOR anon ON COLUMN "Super-Weird Column Names !!!"." leading_space"
  IS 'MASKED WITH VALUE NULL';
SECURITY LABEL FOR anon ON COLUMN "Super-Weird Column Names !!!"."trailing_space "
  IS 'MASKED WITH VALUE NULL';
SECURITY LABEL FOR anon ON COLUMN "Super-Weird Column Names !!!"."123abc!!!!"
  IS 'MASKED WITH VALUE NULL';
SECURITY LABEL FOR anon ON COLUMN "Super-Weird Column Names !!!"."col""with""quotes"
  IS 'MASKED WITH VALUE NULL';

GRANT SELECT ON TABLE "Super-Weird Column Names !!!" TO jimmy;

SET ROLE jimmy;

COPY "Super-Weird Column Names !!!" TO stdout;

ROLLBACK;
