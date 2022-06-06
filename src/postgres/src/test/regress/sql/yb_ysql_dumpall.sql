CREATE TABLESPACE tsp1 LOCATION '/data';
CREATE TABLESPACE tsp2 WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"datacenter1","zone":"rack1","min_num_replicas":1}]}');
CREATE TABLESPACE tsp_dropped WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":2}]}');
CREATE TABLESPACE tsp_unused WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"dc_unused","zone":"z_unused","min_num_replicas":1}]}');

CREATE TABLE table1(id int) TABLESPACE tsp1;
CREATE INDEX idx1 on table1(id) TABLESPACE tsp2;
CREATE TABLE table2(name varchar) TABLESPACE tsp2;
CREATE INDEX idx2 on table2(name) TABLESPACE tsp1;

CREATE TABLEGROUP grp_without_spc;
CREATE TABLEGROUP grp_with_spc TABLESPACE tsp1;
CREATE TABLE tbl_with_grp_with_spc (a int) WITH (autovacuum_enabled = true) TABLEGROUP grp_with_spc;

DROP TABLESPACE tsp_dropped;
