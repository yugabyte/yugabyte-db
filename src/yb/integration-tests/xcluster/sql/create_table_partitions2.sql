--
-- Partitions
--

-- Taken from Postgres test/regress/sql/create_table.sql
-- Tests some basic create table commands with partitions.

-- check schema propagation from parent

CREATE TABLE parted (
	a text,
	b int NOT NULL DEFAULT 0,
	CONSTRAINT check_a CHECK (length(a) > 0)
) PARTITION BY LIST (a);

CREATE TABLE part_a PARTITION OF parted FOR VALUES IN ('a');

-- only inherited attributes (never local ones)
SELECT attname, attislocal, attinhcount FROM pg_attribute
  WHERE attrelid = 'part_a'::regclass and attnum > 0
  ORDER BY attnum;

-- able to specify column default, column constraint, and table constraint

\set ON_ERROR_STOP off
-- first check the "column specified more than once" error
CREATE TABLE part_b PARTITION OF parted (
	b NOT NULL,
	b DEFAULT 1,
	b CHECK (b >= 0),
	CONSTRAINT check_a CHECK (length(a) > 0)
) FOR VALUES IN ('b');
\set ON_ERROR_STOP on

CREATE TABLE part_b PARTITION OF parted (
	b NOT NULL DEFAULT 1 CHECK (b >= 0),
	CONSTRAINT check_a CHECK (length(a) > 0)
) FOR VALUES IN ('b');
-- conislocal should be false for any merged constraints
SELECT conislocal, coninhcount FROM pg_constraint WHERE conrelid = 'part_b'::regclass AND conname = 'check_a';

-- specify PARTITION BY for a partition
CREATE TABLE part_c PARTITION OF parted (b WITH OPTIONS NOT NULL DEFAULT 0) FOR VALUES IN ('c') PARTITION BY RANGE ((b));

-- create a level-2 partition
CREATE TABLE part_c_1_10 PARTITION OF part_c FOR VALUES FROM (1) TO (10);

-- check that NOT NULL and default value are inherited correctly
create table parted_notnull_inh_test (a int default 1, b int not null default 0) partition by list (a);
create table parted_notnull_inh_test1 partition of parted_notnull_inh_test (a not null, b default 1) for values in (1);
-- note that while b's default is overriden, a's default is preserved
\d parted_notnull_inh_test1

-- check for a conflicting COLLATE clause
create table parted_collate_must_match (a text collate "C", b text collate "C")
  partition by range (a);
-- on the partition key
create table parted_collate_must_match1 partition of parted_collate_must_match
  (a collate "POSIX") for values from ('a') to ('m');
-- on another column
create table parted_collate_must_match2 partition of parted_collate_must_match
  (b collate "POSIX") for values from ('m') to ('z');

-- check that we get the expected partition constraints
CREATE TABLE range_parted4 (a int, b int, c int) PARTITION BY RANGE (abs(a), abs(b), c);
CREATE TABLE unbounded_range_part PARTITION OF range_parted4 FOR VALUES FROM (MINVALUE, MINVALUE, MINVALUE) TO (MAXVALUE, MAXVALUE, MAXVALUE);

-- YB issue - GHI #14575.
-- -- user-defined operator class in partition key
-- CREATE FUNCTION my_int4_sort(int4,int4) RETURNS int LANGUAGE sql
--   AS $$ SELECT CASE WHEN $1 = $2 THEN 0 WHEN $1 > $2 THEN 1 ELSE -1 END; $$;
-- CREATE OPERATOR CLASS test_int4_ops FOR TYPE int4 USING btree AS
--   OPERATOR 1 < (int4,int4), OPERATOR 2 <= (int4,int4),
--   OPERATOR 3 = (int4,int4), OPERATOR 4 >= (int4,int4),
--   OPERATOR 5 > (int4,int4), FUNCTION 1 my_int4_sort(int4,int4);
-- CREATE TABLE partkey_t (a int4) PARTITION BY RANGE (a test_int4_ops);
-- CREATE TABLE partkey_t_1 PARTITION OF partkey_t FOR VALUES FROM (0) TO (1000);
-- INSERT INTO partkey_t VALUES (100);
-- INSERT INTO partkey_t VALUES (200);

-- partitions mixing temporary and permanent relations
create table perm_parted (a int) partition by list (a);
create temporary table temp_parted (a int) partition by list (a);
-- create table perm_part partition of temp_parted default; -- error
-- create temp table temp_part partition of perm_parted default; -- error
create temp table temp_part partition of temp_parted default; -- ok
drop table temp_parted cascade;
