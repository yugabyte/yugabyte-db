--
-- Partitions
--

-- Taken from Postgres test/regress/sql/create_table.sql
-- Tests some basic create table commands with partitions.

-- check partition bound syntax

CREATE TABLE list_parted (
	a int
) PARTITION BY LIST (a);
-- syntax allows only string literal, numeric literal and null to be
-- specified for a partition bound value
CREATE TABLE part_1 PARTITION OF list_parted FOR VALUES IN ('1');
CREATE TABLE part_2 PARTITION OF list_parted FOR VALUES IN (2);
CREATE TABLE part_null PARTITION OF list_parted FOR VALUES IN (null);

-- check default partition cannot be created more than once
CREATE TABLE part_default PARTITION OF list_parted DEFAULT;

-- specified literal can't be cast to the partition column data type
CREATE TABLE bools (
	a bool
) PARTITION BY LIST (a);

-- specified literal can be cast, but cast isn't immutable
CREATE TABLE moneyp (
	a money
) PARTITION BY LIST (a);
CREATE TABLE moneyp_10 PARTITION OF moneyp FOR VALUES IN ('10');

-- immutable cast should work, though
CREATE TABLE bigintp (
	a bigint
) PARTITION BY LIST (a);
CREATE TABLE bigintp_10 PARTITION OF bigintp FOR VALUES IN (10);

CREATE TABLE range_parted (
	a date
) PARTITION BY RANGE (a);

-- check partition bound syntax for the hash partition
CREATE TABLE hash_parted (
	a int
) PARTITION BY HASH (a);
CREATE TABLE hpart_1 PARTITION OF hash_parted FOR VALUES WITH (MODULUS 10, REMAINDER 0);
CREATE TABLE hpart_2 PARTITION OF hash_parted FOR VALUES WITH (MODULUS 50, REMAINDER 1);
CREATE TABLE hpart_3 PARTITION OF hash_parted FOR VALUES WITH (MODULUS 200, REMAINDER 2);

-- cannot create a table with oids as partition of table without oids
CREATE TABLE no_oids_parted (
	a int
) PARTITION BY RANGE (a) WITHOUT OIDS;
-- CREATE TABLE fail_part PARTITION OF no_oids_parted FOR VALUES FROM (1) TO (10) WITH OIDS;

-- -- If the partitioned table has oids, then the partition must have them.
-- -- If the WITHOUT OIDS option is specified for partition, it is overridden.
-- CREATE TABLE oids_parted (
-- 	a int
-- ) PARTITION BY RANGE (a) WITH OIDS;
-- CREATE TABLE part_forced_oids PARTITION OF oids_parted FOR VALUES FROM (1) TO (10) WITHOUT OIDS;

-- check for partition bound overlap and other invalid specifications

CREATE TABLE list_parted2 (
	a varchar
) PARTITION BY LIST (a);
CREATE TABLE part_null_z PARTITION OF list_parted2 FOR VALUES IN (null, 'z');
CREATE TABLE part_ab PARTITION OF list_parted2 FOR VALUES IN ('a', 'b');
CREATE TABLE list_parted2_def PARTITION OF list_parted2 DEFAULT;

-- check default partition overlap
INSERT INTO list_parted2 VALUES('X');

CREATE TABLE range_parted2 (
	a int
) PARTITION BY RANGE (a);

CREATE TABLE part0 PARTITION OF range_parted2 FOR VALUES FROM (minvalue) TO (1);
CREATE TABLE part1 PARTITION OF range_parted2 FOR VALUES FROM (1) TO (10);
CREATE TABLE part2 PARTITION OF range_parted2 FOR VALUES FROM (20) TO (30);
CREATE TABLE part3 PARTITION OF range_parted2 FOR VALUES FROM (30) TO (40);

-- Create a default partition for range partitioned table
CREATE TABLE range2_default PARTITION OF range_parted2 DEFAULT;

-- Check if the range for default partitions overlap
INSERT INTO range_parted2 VALUES (85);
CREATE TABLE part4 PARTITION OF range_parted2 FOR VALUES FROM (90) TO (100);

-- now check for multi-column range partition key
CREATE TABLE range_parted3 (
	a int,
	b int
) PARTITION BY RANGE (a, (b+1));

CREATE TABLE part00 PARTITION OF range_parted3 FOR VALUES FROM (0, minvalue) TO (0, maxvalue);

CREATE TABLE part10 PARTITION OF range_parted3 FOR VALUES FROM (1, minvalue) TO (1, 1);
CREATE TABLE part11 PARTITION OF range_parted3 FOR VALUES FROM (1, 1) TO (1, 10);
CREATE TABLE part12 PARTITION OF range_parted3 FOR VALUES FROM (1, 10) TO (1, maxvalue);
CREATE TABLE range3_default PARTITION OF range_parted3 DEFAULT;

-- check for partition bound overlap and other invalid specifications for the hash partition
CREATE TABLE hash_parted2 (
	a varchar
) PARTITION BY HASH (a);
CREATE TABLE h2part_1 PARTITION OF hash_parted2 FOR VALUES WITH (MODULUS 4, REMAINDER 2);
CREATE TABLE h2part_2 PARTITION OF hash_parted2 FOR VALUES WITH (MODULUS 8, REMAINDER 0);
CREATE TABLE h2part_3 PARTITION OF hash_parted2 FOR VALUES WITH (MODULUS 8, REMAINDER 4);
CREATE TABLE h2part_4 PARTITION OF hash_parted2 FOR VALUES WITH (MODULUS 8, REMAINDER 5);
