CREATE DATABASE test_tablegroup_partitioned_tables;
\c test_tablegroup_partitioned_tables
CREATE TABLEGROUP tg1;
CREATE TABLE prt (id int PRIMARY KEY, v int) PARTITION BY RANGE (id) TABLEGROUP tg1;
CREATE TABLE prt_p1 PARTITION OF prt FOR VALUES FROM (1) TO (2);
CREATE TABLE IF NOT EXISTS prt_p2 PARTITION OF prt FOR VALUES FROM (2) TO (3);
\dgrt
CREATE TABLE prt_p3 PARTITION OF prt FOR VALUES FROM (3) TO (4) TABLEGROUP tg1;
CREATE TABLE IF NOT EXISTS prt_p4 PARTITION OF prt FOR VALUES FROM (4) TO (5) TABLEGROUP tg1;

CREATE TABLE prt2 (id int PRIMARY KEY, v int) PARTITION BY RANGE (id) WITH (colocation_id='666666') TABLEGROUP tg1;
SELECT * FROM yb_table_properties('prt2'::regclass::oid);

CREATE TABLE prt2_p1 PARTITION OF prt2 FOR VALUES FROM (1) TO (2) WITH (colocation_id='7777777');
SELECT * FROM yb_table_properties('prt2_p1'::regclass::oid);
