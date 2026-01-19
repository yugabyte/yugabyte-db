CREATE TABLESPACE tsp1
 WITH (replica_placement='{"num_replicas": 1, "placement_blocks": [
  {"cloud":"testCloud","region":"testRegion","zone":"testZone1","min_num_replicas":1}]}');

CREATE TABLESPACE tsp2
 WITH (replica_placement='{"num_replicas": 1, "placement_blocks": [
  {"cloud":"testCloud","region":"testRegion","zone":"testZone2","min_num_replicas":1}]}');

CREATE TABLESPACE tsp3
 WITH (replica_placement='{"num_replicas": 1, "placement_blocks": [
  {"cloud":"testCloud","region":"testRegion","zone":"testZone3","min_num_replicas":1}]}');

CREATE DATABASE colo_tables WITH COLOCATION=true;
\c colo_tables;

CREATE TABLE t1 (col int) TABLESPACE tsp1;
CREATE TABLE t2 (col int) TABLESPACE tsp2;
CREATE TABLE t3 (col int) TABLESPACE tsp1;
CREATE TABLE t4 (col int) ;
CREATE TABLE t5 (col int) TABLESPACE tsp3;
CREATE TABLE t6 (col int) TABLESPACE tsp2;

CREATE TABLE t7 (a int, b int, c int, d int PRIMARY KEY) TABLESPACE tsp1;
CREATE INDEX i1 ON t7(a);
CREATE INDEX i2 ON t7(b) TABLESPACE tsp2;
CREATE INDEX i3 ON t7(c) TABLESPACE tsp2;
CREATE INDEX i21 on t2(col) TABLESPACE tsp3;
CREATE MATERIALIZED VIEW mv1 TABLESPACE tsp3 AS SELECT * FROM t2;