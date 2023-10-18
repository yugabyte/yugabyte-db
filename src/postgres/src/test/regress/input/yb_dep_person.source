--
-- A collection of queries to build the person table.
--
-- The queries are taken from the relevant dependency files.  Since it is
-- faster to run this rather than each file itself (e.g. dependency chain
-- create_function_1, create_type, create_table, copy, create_index), prefer
-- using this.
--

--
-- create_table
--

CREATE TABLE person (
	name 		text,
	age			int4,
	location 	point
);

--
-- copy
--

\getenv abs_srcdir PG_ABS_SRCDIR

--

\set filename :abs_srcdir '/data/person.data'
COPY person FROM :'filename';

--

ANALYZE person;
