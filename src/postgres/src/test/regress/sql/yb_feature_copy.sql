--
-- YB_FEATURE Testsuite: COPY
--
CREATE TABLE x (
	a int,
	b int,
	c text,
	d text,
	e text
);

COPY x (a, b, c, d, e) from stdin;
9999	\N	\\N	\NN	\N
10000	21	31	41	51
\.

COPY x (b, d) from stdin;
1	test_1
\.

COPY x (b, d) from stdin;
2	test_2
3	test_3
4	test_4
5	test_5
\.

COPY x (a, b, c, d, e) from stdin;
10001	22	32	42	52
10002	23	33	43	53
10003	24	34	44	54
10004	25	35	45	55
10005	26	36	46	56
\.

-- non-existent column in column list: should fail
COPY x (xyz) from stdin;

-- too many columns in column list: should fail
COPY x (a, b, c, d, e, d, c) from stdin;

-- missing data: should fail
COPY x from stdin;

\.
COPY x from stdin;
2000	230	23	23
\.
COPY x from stdin;
2001	231	\N	\N
\.

-- extra data: should fail
COPY x from stdin;
2002	232	40	50	60	70	80
\.

-- various COPY options: delimiters, NULL string, encoding
COPY x (b, c, d, e) from stdin delimiter ',' null 'x';
x,45,80,90
x,\x,\\x,\\\x
x,\,,\\\,,\\
\.

COPY x from stdin WITH DELIMITER AS ';' NULL AS '';
3000;;c;;
\.

COPY x from stdin WITH DELIMITER AS ':' NULL AS E'\\X' ENCODING 'sql_ascii';
4000:\X:C:\X:\X
4001:1:empty::
4002:2:null:\X:\X
4003:3:Backslash:\\:\\
4004:4:BackslashX:\\X:\\X
4005:5:N:\N:\N
4006:6:BackslashN:\\N:\\N
4007:7:XX:\XX:\XX
4008:8:Delimiter:\::\:
\.

-- check results of copy in
SELECT * FROM x ORDER BY a,b,c,d;

-- check copy out
COPY (SELECT * FROM x ORDER BY a,b,c,d) TO stdout;
COPY (SELECT c,e FROM x ORDER BY a,b,c,d) TO stdout;
COPY (SELECT b,e FROM x ORDER BY a,b,c,d) TO stdout WITH NULL 'I''m null';

CREATE TABLE y (
	col1 text,
	col2 text
);

INSERT INTO y VALUES ('Jackson, Sam', E'\\h');
INSERT INTO y VALUES ('It is "perfect".',E'\t');
INSERT INTO y VALUES ('', NULL);

COPY (SELECT * FROM y ORDER BY col1,col2) TO stdout WITH CSV;
COPY (SELECT * FROM y ORDER BY col1,col2) TO stdout WITH CSV QUOTE '''' DELIMITER '|';
COPY (SELECT * FROM y ORDER BY col1,col2) TO stdout WITH CSV FORCE QUOTE col2 ESCAPE E'\\' ENCODING 'sql_ascii';
COPY (SELECT * FROM y ORDER BY col1,col2) TO stdout WITH CSV FORCE QUOTE *;

-- Repeat above tests with new 9.0 option syntax

COPY (SELECT * FROM y ORDER BY col1,col2) TO stdout (FORMAT CSV);
COPY (SELECT * FROM y ORDER BY col1,col2) TO stdout (FORMAT CSV, QUOTE '''', DELIMITER '|');
COPY (SELECT * FROM y ORDER BY col1,col2) TO stdout (FORMAT CSV, FORCE_QUOTE (col2), ESCAPE E'\\');
COPY (SELECT * FROM y ORDER BY col1,col2) TO stdout (FORMAT CSV, FORCE_QUOTE *);

\copy (SELECT * FROM y ORDER BY col1,col2) TO stdout (FORMAT CSV)
\copy (SELECT * FROM y ORDER BY col1,col2) TO stdout (FORMAT CSV, QUOTE '''', DELIMITER '|')
\copy (SELECT * FROM y ORDER BY col1,col2) TO stdout (FORMAT CSV, FORCE_QUOTE (col2), ESCAPE E'\\')
\copy (SELECT * FROM y ORDER BY col1,col2) TO stdout (FORMAT CSV, FORCE_QUOTE *)

--test that we read consecutive LFs properly

CREATE TABLE testnl (a int, b text, c int);

COPY testnl FROM stdin CSV;
1,"a field with two LFs

inside",2
\.

-- test end of copy marker
CREATE TABLE testeoc (a text);

COPY testeoc FROM stdin CSV;
a\.
\.b
c\.d
"\."
\.

-- test handling of nonstandard null marker that violates escaping rules

CREATE TABLE testnull (a int, b text);
INSERT INTO testnull VALUES (1, E'\\0'), (NULL, NULL);

COPY (SELECT * FROM testnull ORDER BY a,b) TO stdout WITH NULL AS E'\\0';

COPY testnull FROM stdin WITH NULL AS E'\\0';
42	\\0
\0	\0
\.

SELECT * FROM testnull ORDER BY a,b;

-- Test FORCE_NOT_NULL and FORCE_NULL options
CREATE TABLE forcetest (
    a INT NOT NULL,
    b TEXT NOT NULL,
    c TEXT,
    d TEXT,
    e TEXT
);
\pset null NULL
-- should succeed with no effect ("b" remains an empty string, "c" remains NULL)
BEGIN;
COPY forcetest (a, b, c) FROM STDIN WITH (FORMAT csv, FORCE_NOT_NULL(b), FORCE_NULL(c));
1,,""
\.
COMMIT;
SELECT b, c FROM forcetest WHERE a = 1;
-- should succeed, FORCE_NULL and FORCE_NOT_NULL can be both specified
BEGIN;
COPY forcetest (a, b, c, d) FROM STDIN WITH (FORMAT csv, FORCE_NOT_NULL(c,d), FORCE_NULL(c,d));
2,'a',,""
\.
COMMIT;
SELECT c, d FROM forcetest WHERE a = 2;
-- should fail with not-null constraint violation
BEGIN;
COPY forcetest (a, b, c) FROM STDIN WITH (FORMAT csv, FORCE_NULL(b), FORCE_NOT_NULL(c));
3,,""
\.
ROLLBACK;
-- should fail with "not referenced by COPY" error
BEGIN;
COPY forcetest (d, e) FROM STDIN WITH (FORMAT csv, FORCE_NOT_NULL(b));
ROLLBACK;
-- should fail with "not referenced by COPY" error
BEGIN;
COPY forcetest (d, e) FROM STDIN WITH (FORMAT csv, FORCE_NULL(b));
ROLLBACK;

CREATE TABLE t(k INT PRIMARY KEY, v INT);
CREATE UNIQUE INDEX ON t(v);

-- should fail, non unique primary key
COPY t FROM stdin;
1	1
2	2
2	3
4	4
\.

-- should fail, non unique index
COPY t FROM stdin;
1	1
2	2
3	2
4	4
\.

SELECT COUNT(*) FROM t;

-- clean up
DROP TABLE forcetest;
DROP TABLE x;
DROP TABLE y;
DROP TABLE testnl;
DROP TABLE testeoc;
DROP TABLE testnull;
DROP TABLE t;