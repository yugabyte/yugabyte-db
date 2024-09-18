--
-- CREATE_TABLE
--

-- Taken from Postgres test/regress/sql/create_table.sql
-- Tests some basic create table commands.

--
-- test the "star" operators a bit more thoroughly -- this time,
-- throw in lots of NULL fields...
--
-- a is the type root
-- b and c inherit from a (one-level single inheritance)
-- d inherits from b and c (two-level multiple inheritance)
-- e inherits from c (two-level single inheritance)
-- f inherits from e (three-level single inheritance)
--
CREATE TABLE a_star (
	class		char,
	a 			int4
);

CREATE TABLE b_star (
	b 			text
) INHERITS (a_star);

CREATE TABLE c_star (
	c 			name
) INHERITS (a_star);

CREATE TABLE d_star (
	d 			float8
) INHERITS (b_star, c_star);

CREATE TABLE e_star (
	e 			int2
) INHERITS (c_star);

CREATE TABLE f_star (
	f 			polygon
) INHERITS (e_star);

CREATE TABLE aggtest (
	a 			int2,
	b			float4
);

CREATE TABLE hash_i4_heap (
	seqno 		int4,
	random 		int4
);

CREATE TABLE hash_name_heap (
	seqno 		int4,
	random 		name
);

CREATE TABLE hash_txt_heap (
	seqno 		int4,
	random 		text
);

CREATE TABLE hash_f8_heap (
	seqno		int4,
	random 		float8
);

CREATE TABLE bt_i4_heap (
	seqno 		int4,
	random 		int4
);

CREATE TABLE bt_name_heap (
	seqno 		name,
	random 		int4
);

CREATE TABLE bt_txt_heap (
	seqno 		text,
	random 		int4
);

CREATE TABLE bt_f8_heap (
	seqno 		float8,
	random 		int4
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

CREATE TABLE unknowntab (
	u unknown    -- fail
);

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

-- create an extra wide table to test for issues related to that
-- (temporarily hide query, to avoid the long CREATE TABLE stmt)
\set ECHO none
SELECT 'CREATE TABLE extra_wide_table(firstc text, '|| array_to_string(array_agg('c'||i||' bool'),',')||', lastc text);'
FROM generate_series(1, 1100) g(i)
\gexec
\set ECHO all
INSERT INTO extra_wide_table(firstc, lastc) VALUES('first col', 'last col');
SELECT firstc, lastc FROM extra_wide_table;
