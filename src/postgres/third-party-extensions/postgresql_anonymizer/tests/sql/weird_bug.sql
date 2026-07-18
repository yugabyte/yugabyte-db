CREATE EXTENSION IF NOT EXISTS anon CASCADE;
SELECT anon.start_dynamic_masking();

CREATE TABLE t1 (
	id TEXT,
	name TEXT,
	creditcard TEXT,
	fk_compx TEXT --remove 'x' will fix the problem
);

-- remove one the 2 comments will fix the problem
COMMENT ON COLUMN t1.name IS '  MASKED WITH FUNCTION anon.fake_last_name() ';
COMMENT ON COLUMN t1.creditcard IS '  MASKED    WITH    FUNCTION         anon.random_string(12)';

CREATE TABLE "T2" (
	id_company SERIAL, -- change SERIAL to TEXT will fix the problem
	"I" TEXT, -- change "I" to i will  fix the problem
	COMPANY TEXT
);

--  CLEAN
DROP TABLE "T2" CASCADE;
DROP TABLE t1 CASCADE;
DROP EXTENSION anon CASCADE;
