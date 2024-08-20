
--
-- Partitioned tables
--

-- Taken from Postgres test/regress/sql/create_table.sql
-- Tests some basic create partitioned table commands.

-- cannot combine INHERITS and PARTITION BY (although grammar allows)
CREATE TABLE partitioned (
	a int
) INHERITS (some_table) PARTITION BY LIST (a);

-- cannot use more than 1 column as partition key for list partitioned table
CREATE TABLE partitioned (
	a1 int,
	a2 int
) PARTITION BY LIST (a1, a2);	-- fail

-- unsupported constraint type for partitioned tables
CREATE TABLE partitioned (
	a int,
	EXCLUDE USING gist (a WITH &&)
) PARTITION BY RANGE (a);

-- prevent using prohibited expressions in the key
CREATE FUNCTION retset (a int) RETURNS SETOF int AS $$ SELECT 1; $$ LANGUAGE SQL IMMUTABLE;
CREATE TABLE partitioned (
	a int
) PARTITION BY RANGE (retset(a));
DROP FUNCTION retset(int);

CREATE TABLE partitioned (
	a int
) PARTITION BY RANGE ((avg(a)));

CREATE TABLE partitioned (
	a int,
	b int
) PARTITION BY RANGE ((avg(a) OVER (PARTITION BY b)));

CREATE TABLE partitioned (
	a int
) PARTITION BY LIST ((a LIKE (SELECT 1)));

CREATE TABLE partitioned (
	a int
) PARTITION BY RANGE (('a'));

CREATE FUNCTION const_func () RETURNS int AS $$ SELECT 1; $$ LANGUAGE SQL IMMUTABLE;
CREATE TABLE partitioned (
	a int
) PARTITION BY RANGE (const_func());
DROP FUNCTION const_func();

-- only accept valid partitioning strategy
CREATE TABLE partitioned (
    a int
) PARTITION BY MAGIC (a);

-- specified column must be present in the table
CREATE TABLE partitioned (
	a int
) PARTITION BY RANGE (b);

-- cannot use system columns in partition key
CREATE TABLE partitioned (
	a int
) PARTITION BY RANGE (xmin);

-- functions in key must be immutable
CREATE FUNCTION immut_func (a int) RETURNS int AS $$ SELECT a + random()::int; $$ LANGUAGE SQL;
CREATE TABLE partitioned (
	a int
) PARTITION BY RANGE (immut_func(a));
DROP FUNCTION immut_func(int);

-- cannot contain whole-row references
CREATE TABLE partitioned (
	a	int
) PARTITION BY RANGE ((partitioned));

-- prevent using columns of unsupported types in key (type must have a btree operator class)
CREATE TABLE partitioned (
	a point
) PARTITION BY LIST (a);
CREATE TABLE partitioned (
	a point
) PARTITION BY LIST (a point_ops);
CREATE TABLE partitioned (
	a point
) PARTITION BY RANGE (a);
CREATE TABLE partitioned (
	a point
) PARTITION BY RANGE (a point_ops);

-- cannot add NO INHERIT constraints to partitioned tables
CREATE TABLE partitioned (
	a int,
	CONSTRAINT check_a CHECK (a > 0) NO INHERIT
) PARTITION BY RANGE (a);

-- some checks after successful creation of a partitioned table
CREATE FUNCTION plusone(a int) RETURNS INT AS $$ SELECT a+1; $$ LANGUAGE SQL;

CREATE TABLE partitioned (
	a int,
	b int,
	c text,
	d text
) PARTITION BY RANGE (a oid_ops, plusone(b), c collate "default", d collate "C");

-- check relkind
SELECT relkind FROM pg_class WHERE relname = 'partitioned';

-- prevent a function referenced in partition key from being dropped
DROP FUNCTION plusone(int);

-- partitioned table cannot participate in regular inheritance
CREATE TABLE partitioned2 (
	a int,
	b text
) PARTITION BY RANGE ((a+1), substr(b, 1, 5));
CREATE TABLE fail () INHERITS (partitioned2);

-- Partition key in describe output
\d partitioned
\d+ partitioned2

INSERT INTO partitioned2 VALUES (1, 'hello');
CREATE TABLE part2_1 PARTITION OF partitioned2 FOR VALUES FROM (-1, 'aaaaa') TO (100, 'ccccc');
\d+ part2_1
