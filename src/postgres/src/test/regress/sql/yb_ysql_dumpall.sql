CREATE TABLESPACE x LOCATION '/data';
CREATE TABLESPACE y WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":2}]}');
CREATE TABLESPACE z WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"datacenter1","zone":"rack1","min_num_replicas":1}]}');

CREATE TABLE table1(id int) TABLESPACE x;
CREATE INDEX idx1 on table1(id) TABLESPACE z;
CREATE TABLE table2(name varchar) TABLESPACE z;
CREATE INDEX idx2 on table2(name) TABLESPACE x;

DROP TABLESPACE y;
