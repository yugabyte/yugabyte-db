--
-- A collection of queries to build the person table.
--
-- The queries are taken from the relevant dependency files.  Since it is
-- faster to run this rather than each file itself (e.g. dependency chain
-- test_setup, create_index), prefer using this.
--

--
-- test_setup
--

\getenv abs_srcdir PG_ABS_SRCDIR

--

CREATE TABLE person (
	name 		text,
	age			int4,
	location 	point
);

\set filename :abs_srcdir '/data/person.data'
COPY person FROM :'filename';
VACUUM ANALYZE person;
