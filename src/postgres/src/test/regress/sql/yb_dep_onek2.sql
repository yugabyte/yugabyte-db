--
-- A collection of queries to build the onek2 table.
--
-- The queries are taken from the relevant dependency files.  Since it is
-- faster to run this rather than each file itself (e.g. dependency chain
-- create_function_1, create_type, create_table, copy, create_index), prefer
-- using this.
--
-- DEPENDENCY: this file must be run after onek has been populated (by
-- yb_dep_onek).
--

--
-- create_misc
--

SELECT * INTO TABLE onek2 FROM onek;

--
-- yb_pg_create_index
-- (With modification to make them all nonconcurrent for performance.)
--

CREATE INDEX NONCONCURRENTLY onek2_u1_prtl ON onek2 USING btree(unique1 int4_ops ASC)
	where unique1 < 20 or unique1 > 980;

CREATE INDEX NONCONCURRENTLY onek2_u2_prtl ON onek2 USING btree(unique2 int4_ops ASC)
	where stringu1 < 'B';

CREATE INDEX NONCONCURRENTLY onek2_stu1_prtl ON onek2 USING btree(stringu1 name_ops ASC)
	where onek2.stringu1 >= 'J' and onek2.stringu1 < 'K';

--
-- select
--

ANALYZE onek2;
