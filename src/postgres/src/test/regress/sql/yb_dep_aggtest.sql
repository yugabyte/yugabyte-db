--
-- A collection of queries to build the aggtest table.
--
-- The queries are taken from the relevant dependency files.  Since it is
-- faster to run this rather than each file itself (e.g. dependency chain
-- create_function_1, create_type, create_table, copy, create_index), prefer
-- using this.
--

--
-- create_table
--

CREATE TABLE aggtest (
	a 			int2,
	b			float4
);

--
-- copy
--

\getenv abs_srcdir PG_ABS_SRCDIR

--

\set filename :abs_srcdir '/data/agg.data'
COPY aggtest FROM :'filename';

--

ANALYZE aggtest;
