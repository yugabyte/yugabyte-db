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
) WITH OIDS;

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
) INHERITS (person) WITH OIDS;


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

CREATE TABLE patient (
	name		text,
	age		    int4,
	dob      	date
) WITH (fillfactor=40);

CREATE TABLE planetrip (
	origin		text,
	dest	    text,
	day         date,
	depart     	time
) WITH (user_catalog_table=true);

CREATE TABLE client (
	name        text,
	phonenum    int8,
	deadline    date
) WITH (oids=false);

CREATE TABLE tbl1 (
	a			int4 primary key
) SPLIT (INTO 20 TABLETS);

CREATE TABLE tbl2 (
	a			int4,
	primary key (a asc)
) SPLIT (AT VALUES (4), (25), (100));

CREATE TABLE tbl3 (
	a			int4,
	primary key (a asc)
) SPLIT (AT VALUES (25), (100), (4));

CREATE TABLE tbl4 (
	a			int4,
	b			text,
	primary key (a asc, b)
) SPLIT (AT VALUES (1, 'c'), (1, 'cb'), (2, 'a'));

CREATE TABLE tbl5 (
	a			int4,
	b			text,
	primary key (b asc)
) SPLIT (AT VALUES ('a'), ('aba'), ('ac'));

CREATE TABLE tbl6 (
	a			int4,
	b			text,
	primary key (b asc)
) SPLIT (AT VALUES ('a'), (2, 'aba'), ('ac'));

CREATE TABLE tbl7 (
	a			int4,
	primary key (a asc)
) SPLIT (AT VALUES ('a'), ('b'), ('c'));

CREATE TABLE tbl8 (
	a			text,
	primary key (a asc)
) SPLIT (AT VALUES (100), (1000), (10000));

CREATE TABLE tbl9 (
	a			int4,
	primary key (a hash)
) SPLIT (AT VALUES (100), (1000), (10000));

CREATE TEMPORARY TABLE tbl10 (
	a			int4 primary key
) SPLIT (INTO 20 TABLETS);

CREATE TABLE tbl11 (
	a			int4,
	b			int4,
	c			int4,
	primary key (a asc, b desc)
) SPLIT (AT VALUES (-7, 1), (0, 0), (23, 4));

CREATE TABLE tbl12 (
	a			int4,
	b			text,
	primary key (b desc)
) SPLIT (AT VALUES ('bienvenidos'), ('goodbye'), ('hello'), ('hola'));

CREATE TABLE tbl12 (
	a			text,
	b			date,
	c			time
) SPLIT (INTO 9 TABLETS);

CREATE TABLE tbl13 (
	a			int4,
	primary key (a asc)
) SPLIT (AT VALUES (MINVALUE), (0), (MAXVALUE));
