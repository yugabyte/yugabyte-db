--
-- A collection of queries to build the tenk2 table.
--
-- The queries are taken from the relevant dependency files.  Since it is
-- faster to run this rather than each file itself (e.g. dependency chain
-- create_function_1, create_type, create_table, copy, create_index), prefer
-- using this.
--
-- DEPENDENCY: this file must be run after tenk1 has been populated (by
-- yb_dep_tenk1).
--

--
-- create_table
--

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

--
-- create_misc
--

INSERT INTO tenk2 SELECT * FROM tenk1;

--
-- yb_pg_create_index
-- (With modification to make them all nonconcurrent for performance.)
--

CREATE INDEX NONCONCURRENTLY tenk2_unique1 ON tenk2 USING btree(unique1 int4_ops ASC);

CREATE INDEX NONCONCURRENTLY tenk2_unique2 ON tenk2 USING btree(unique2 int4_ops ASC);

CREATE INDEX NONCONCURRENTLY tenk2_hundred ON tenk2 USING btree(hundred int4_ops ASC);
