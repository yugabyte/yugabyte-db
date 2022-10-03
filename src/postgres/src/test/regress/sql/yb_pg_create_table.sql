--
-- CREATE_TABLE
--

--
-- CLASS DEFINITIONS
--
CREATE TABLE hobbies_r (
	name		text,
	person 		text
);

CREATE TABLE equipment_r (
	name 		text,
	hobby		text
);

CREATE TABLE onek (
	unique1		int4,
	unique2		int4,
	two			int4,
	four		int4,
	ten			int4,
	twenty		int4,
	hundred		int4,
	thousand	int4,
	twothousand	int4,
	fivethous	int4,
	tenthous	int4,
	odd			int4,
	even		int4,
	stringu1	name,
	stringu2	name,
	string4		name
);

CREATE TABLE tenk1 (
	unique1		int4,
	unique2		int4,
	two			int4,
	four		int4,
	ten			int4,
	twenty		int4,
	hundred		int4,
	thousand	int4,
	twothousand	int4,
	fivethous	int4,
	tenthous	int4,
	odd			int4,
	even		int4,
	stringu1	name,
	stringu2	name,
	string4		name
);

CREATE TABLE tenk2 (
	unique1 	int4,
	unique2 	int4,
	two 	 	int4,
	four 		int4,
	ten			int4,
	twenty 		int4,
	hundred 	int4,
	thousand 	int4,
	twothousand int4,
	fivethous 	int4,
	tenthous	int4,
	odd			int4,
	even		int4,
	stringu1	name,
	stringu2	name,
	string4		name
);


CREATE TABLE person (
	name 		text,
	age			int4,
	location 	point
);


CREATE TABLE emp (
	salary 		int4,
	manager 	name
) INHERITS (person);


CREATE TABLE student (
	gpa 		float8
) INHERITS (person);


CREATE TABLE stud_emp (
	percent 	int4
) INHERITS (emp, student);


CREATE TABLE city (
	name		name,
	location 	box,
	budget 		city_budget
);

CREATE TABLE dept (
	dname		name,
	mgrname 	text
);

CREATE TABLE slow_emp4000 (
	home_base	 box
);

CREATE TABLE fast_emp4000 (
	home_base	 box
);

CREATE TABLE road (
	name		text,
	thepath 	path
);

CREATE TABLE ihighway () INHERITS (road);

CREATE TABLE shighway (
	surface		text
) INHERITS (road);

CREATE TABLE real_city (
	pop			int4,
	cname		text,
	outline 	path
);

CREATE TABLE aggtest (
	a			int2,
	b			float4
);

CREATE TABLE array_op_test (
	seqno		int4,
	i			int4[],
	t			text[]
);

CREATE TABLE array_index_op_test (
	seqno		int4,
	i			int4[],
	t			text[]
);

CREATE TABLE testjsonb (
       j jsonb
);

-- TODO(jason): uncomment when issue #1975 is closed or closing.
-- CREATE TABLE unknowntab (
-- 	u unknown    -- fail
-- );

CREATE TYPE unknown_comptype AS (
	u unknown    -- fail
);

CREATE TABLE IF NOT EXISTS test_tsvector(
	t text,
	a tsvector
);

CREATE TABLE IF NOT EXISTS test_tsvector(
	t text
);

-- invalid: non-lowercase quoted reloptions identifiers
CREATE TABLE tas_case WITH ("Fillfactor" = 10) AS SELECT 1 a;
CREATE TABLE tas_case (a text) WITH ("Oids" = true);

CREATE UNLOGGED TABLE unlogged1 (a int primary key);			-- OK
CREATE TEMPORARY TABLE unlogged2 (a int primary key);			-- OK
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^unlogged\d' ORDER BY relname;
REINDEX INDEX unlogged2_pkey;
SELECT relname, relkind, relpersistence FROM pg_class WHERE relname ~ '^unlogged\d' ORDER BY relname;
DROP TABLE unlogged2;
CREATE TABLE pg_temp.implicitly_temp (a int primary key);		-- OK
CREATE TEMP TABLE explicitly_temp (a int primary key);			-- also OK
CREATE TEMP TABLE pg_temp.doubly_temp (a int primary key);		-- also OK
CREATE TEMP TABLE public.temp_to_perm (a int primary key);		-- not OK
