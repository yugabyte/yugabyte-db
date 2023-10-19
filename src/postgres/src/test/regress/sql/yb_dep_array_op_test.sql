--
-- A collection of queries to build the array_op_test table.
--
-- The queries are taken from the relevant dependency files.  Since it is
-- faster to run this rather than each file itself (e.g. dependency chain
-- create_function_1, create_type, create_table, copy, create_index), prefer
-- using this.
--

--
-- create_table
--

CREATE TABLE array_op_test (
	seqno		int4,
	i			int4[],
	t			text[]
);

--
-- copy
--

\getenv abs_srcdir PG_ABS_SRCDIR

--

\set filename :abs_srcdir '/data/array.data'
COPY array_op_test FROM :'filename';

--

ANALYZE array_op_test;
