-- Verify that for all 3 types of partitioning, the primary key
-- is correctly inherited by the partitions.
-- Hash partitioning where primary key contains both hash and range components.
CREATE TABLE test(h1 int NOT NULL,
                  h2 int NOT NULL,
                  h3 int NOT NULL,
                  v1 int,
                  primary key(h1, h2, h3))
PARTITION BY HASH(h1);

-- Create a partition table.
CREATE TABLE test_1 PARTITION of test FOR VALUES WITH (modulus 2, remainder 0);

-- Create a table without a primary key and attach it as a partition.
CREATE TABLE test_2(h1 int NOT NULL,
                  h2 int NOT NULL,
                  h3 int NOT NULL,
                  v1 int);
ALTER TABLE test ATTACH PARTITION test_2 FOR VALUES WITH (modulus 2, remainder 1);

\d test_1;
\d test_2;

INSERT INTO test VALUES (1, 4, 3);

-- Fail because primary key constraint is violated.
INSERT INTO test VALUES (1, 4, 3);
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE h1 = 1 AND h2 = 4 AND h3 = 3;
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE h1 = 1 ORDER BY h2;
EXPLAIN (COSTS OFF) SELECT * FROM test WHERE h1 = 1 AND h2 = 4 ORDER BY h3;

-- LIST partitioning where primary key contain only hash components.
CREATE TABLE person (
    person_id         int not null,
    country           text,
    name  text,
    age int,
    PRIMARY KEY((person_id, country) HASH))
PARTITION BY LIST (country);

CREATE TABLE person_americas
    PARTITION OF person
    FOR VALUES IN ('United States', 'Brazil', 'Mexico', 'Columbia');

CREATE TABLE person_apac (
    person_id         int not null,
    country           text not null,
    name  text,
    age int);
ALTER TABLE person ATTACH PARTITION person_apac FOR VALUES IN ('India', 'Singapore');

\d person_americas;
\d person_apac;

INSERT INTO person_americas VALUES (1, 'United States', 'Jane Doe', 23);
-- Fail due to primary key constraint failure.
INSERT INTO person_americas VALUES (1, 'United States', 'Jane Doe', 23);

EXPLAIN (COSTS OFF) SELECT * FROM person WHERE person_id=1 AND country='United States';

-- Range partitioning where primary key contains only range components.
CREATE TABLE parted (a int, b text, PRIMARY KEY (a ASC, b DESC)) PARTITION BY RANGE(a);
CREATE TABLE part_a_1_5 PARTITION OF parted (a, b) FOR VALUES FROM (1) TO (5);
CREATE TABLE part_a_5_10 PARTITION OF parted (a, b) FOR VALUES FROM (5) TO (10);

\d part_a_1_5;
\d part_a_5_10;

INSERT INTO parted VALUES (1, '1');

-- Fail
INSERT INTO parted VALUES (1, '1');
EXPLAIN (COSTS OFF) SELECT * FROM parted WHERE a = 1 ORDER BY b DESC;
EXPLAIN (COSTS OFF) SELECT * FROM parted ORDER BY a;

-- Test creating a partition with a different primary key.
CREATE TABLE part_a_15_20 PARTITION OF parted (a, b, PRIMARY KEY(a HASH)) FOR VALUES FROM (15) TO (20);

-- Test attaching a partition with a different primary key.
CREATE TABLE part_a_20_25 (a int, b text NOT NULL, PRIMARY KEY (a ASC));
ALTER TABLE parted ATTACH PARTITION part_a_20_25 FOR VALUES FROM (20) TO (25);

-- Create a partition table without a primary key and attach it as a partition.
CREATE TABLE part_a_25_40 (a int NOT NULL, b text NOT NULL) PARTITION BY range (a);
CREATE TABLE part_a_25_35 PARTITION OF part_a_25_40 FOR VALUES FROM (25) TO (35) PARTITION BY range (a);
CREATE TABLE part_a_25_30 PARTITION OF part_a_25_35 FOR VALUES FROM (25) TO (30);
CREATE TABLE part_a_30_35 PARTITION OF part_a_25_35 FOR VALUES FROM (30) TO (35);
CREATE TABLE part_a_35_40 PARTITION OF part_a_25_40 FOR VALUES FROM (35) TO (40);
ALTER TABLE parted ATTACH PARTITION part_a_25_40 FOR VALUES FROM (25) TO (40);
\d part_a_25_40;
\d part_a_25_35;
\d part_a_25_30;
INSERT INTO parted VALUES (26, '26');
INSERT INTO parted VALUES (26, '26'); -- should fail.
INSERT INTO parted VALUES (31, '31');
INSERT INTO parted VALUES (31, '31'); -- should fail.
INSERT INTO parted VALUES (36, '36');
INSERT INTO parted VALUES (36, '36'); -- should fail.
-- Test a complicated situation where the attribute numbers of partition tables
-- and partitioned tables are different.
CREATE TABLE col_order_change (
        a text,
        b bigint,
        c numeric,
        d int,
        e varchar,
	PRIMARY KEY ((a,b) HASH, c ASC, d DESC)
) PARTITION BY RANGE (a, b);

CREATE TABLE col_order_change_part (e varchar, c numeric, a text, b bigint, d int NOT NULL);
ALTER TABLE col_order_change_part DROP COLUMN e, DROP COLUMN c, DROP COLUMN a;
ALTER TABLE col_order_change_part ADD COLUMN c numeric NOT NULL, ADD COLUMN e varchar NOT NULL, ADD COLUMN a text NOT NULL;
ALTER TABLE col_order_change_part DROP COLUMN b;
ALTER TABLE col_order_change_part ADD COLUMN b bigint NOT NULL;
ALTER TABLE col_order_change ATTACH PARTITION col_order_change_part FOR VALUES FROM ('a', 10) TO ('a', 20);
\d col_order_change_part;

-- Cleanup
DROP TABLE col_order_change;
DROP TABLE parted;
DROP TABLE person;
DROP TABLE test;
DROP TABLE part_a_20_25;
