-- This test cannot be run in a single transaction
-- This test must be run on a database named 'contrib_regression'

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

-- INIT

SELECT anon.start_dynamic_masking();

SELECT anon.start_dynamic_masking();

-- Table `people`
CREATE TABLE people (
  id SERIAL UNIQUE,
  name TEXT,
  "CreditCard" TEXT,
  fk_company INTEGER
);

INSERT INTO people
VALUES (1,'Schwarzenegger','1234567812345678', 1991);


SECURITY LABEL FOR anon ON COLUMN people.name
IS 'MASKED WITH FUNCTION anon.fake_last_name() ';

SECURITY LABEL FOR anon ON COLUMN people."CreditCard"
IS 'MASKED WITH FUNCTION         anon.random_string(12)';

-- Table `CoMPaNy`
CREATE TABLE "CoMPaNy" (
  id_company SERIAL UNIQUE,
  "IBAN" TEXT,
  NAME TEXT
);

INSERT INTO "CoMPaNy"
VALUES (1991,'12345677890','Cyberdyne Systems');

SECURITY LABEL FOR anon ON COLUMN "CoMPaNy"."IBAN"
IS 'MASKED WITH FUNCTION anon.partial("IBAN", 0, $$***********$$, 4 )     ';

SECURITY LABEL FOR anon ON COLUMN "CoMPaNy".NAME
IS 'MASKED WITH VALUE $$CONFIDENTIAL$$';

-- BUG #51 :
CREATE TABLE test_type_casts(
  last_name VARCHAR(30)
);

SECURITY LABEL FOR anon ON column test_type_casts.last_name
IS 'MASKED WITH FUNCTION anon.fake_last_name()';

-- Table `work`
CREATE TABLE work (
  id_work SERIAL,
  fk_employee INTEGER NOT NULL,
  fk_company INTEGER NOT NULL,
  first_day DATE NOT NULL,
  last_day DATE,
  FOREIGN KEY (fk_employee) references people(id),
  FOREIGN KEY (fk_company) references "CoMPaNy"(id_company)
);

INSERT INTO work
VALUES ( 1, 1 , 1991, DATE '1985-05-25',NULL);

SELECT count(*) = 5  FROM anon.pg_masks;

SELECT masking_function = 'anon.partial("IBAN", 0, $$***********$$, 4 )'
  FROM anon.pg_masks
  WHERE attname = 'IBAN';

SELECT name != 'Cyberdyne Systems' FROM mask."CoMPaNy" WHERE id_company=1991;

SELECT name != 'Schwarzenegger' FROM mask.people WHERE id = 1;

-- ROLE

CREATE ROLE skynet LOGIN PASSWORD 'x';
SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';

-- FORCE update because SECURITY LABEL doesn't trigger the Event Trigger
SELECT anon.mask_update();


-- We're using an external connection instead of `SET ROLE`
-- Because we need the tricky search_path
\! PGPASSWORD=x psql contrib_regression -U skynet -c 'SHOW search_path;'

-- This test should fail
--
-- We're catching the output because the message
-- PG10 would say "ERROR:  permission denied for relation people"
-- PG11 would say "ERROR:  permission denied for table people"
--
\! PGPASSWORD=x psql contrib_regression -U skynet -c "SELECT * FROM public.people;"  2>&1 | grep --silent 'ERROR:  permission denied' && echo 'ERROR:  permission denied'

\! PGPASSWORD=x psql contrib_regression -U skynet -c "SELECT name != 'Schwarzenegger' FROM people WHERE id = 1;"

\! PGPASSWORD=x psql contrib_regression -U skynet -c "SELECT name != 'Cyberdyne Systems' FROM \"CoMPaNy\" WHERE id_company=1991;"


-- A maked role cannot modify a table containing a mask column

\! PGPASSWORD=x psql contrib_regression -U skynet -c "DELETE FROM people;" 2>&1 | grep --silent 'ERROR:  permission denied' && echo 'ERROR:  permission denied'

\! PGPASSWORD=x psql contrib_regression -U skynet -c "UPDATE people SET name = 'check' WHERE name ='Schwarzenegger';"

\! PGPASSWORD=x psql contrib_regression -U skynet -c "INSERT INTO people VALUES (1,'Schwarzenegger','1234567812345678', 1991);" ;

\! PGPASSWORD=x psql contrib_regression -U skynet -c "DELETE FROM work;" 2>&1 | grep --silent 'ERROR:  permission denied' && echo 'ERROR:  permission denied'


-- A masked role cannot access the stats of a masked column
\! PGPASSWORD=x psql contrib_regression -U skynet -c "SELECT count(histogram_bounds)=0 FROM pg_stats WHERE tablename='people' AND attname='name';"

-- Bug #254 - A masked role can use pseudonymizing functions
\! PGPASSWORD=x psql contrib_regression -U skynet -c "SELECT anon.pseudo_company(0,'salt');"


--  CLEAN

DROP TABLE test_type_casts CASCADE;
DROP TABLE work CASCADE;
DROP TABLE "CoMPaNy" CASCADE;
DROP TABLE people CASCADE;

DROP EXTENSION anon CASCADE;
DROP SCHEMA mask;

REASSIGN OWNED BY skynet TO postgres;
DROP OWNED BY skynet CASCADE;
DROP ROLE skynet;
