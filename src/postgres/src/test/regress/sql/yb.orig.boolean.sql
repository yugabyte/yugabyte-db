CREATE TABLE bools (b bool);
INSERT INTO bools VALUES (null);
CREATE INDEX NONCONCURRENTLY asc_nulls_last ON bools (b ASC NULLS LAST); -- = (b ASC)
CREATE INDEX NONCONCURRENTLY asc_nulls_first ON bools (b ASC NULLS FIRST);
CREATE INDEX NONCONCURRENTLY desc_nulls_last ON bools (b DESC NULLS LAST);
CREATE INDEX NONCONCURRENTLY desc_nulls_first ON bools (b DESC NULLS FIRST); -- = (b DESC)

/*+IndexOnlyScan(bools asc_nulls_last)*/ SELECT * FROM bools;
/*+IndexOnlyScan(bools asc_nulls_first)*/ SELECT * FROM bools;
/*+IndexOnlyScan(bools desc_nulls_last)*/ SELECT * FROM bools;
/*+IndexOnlyScan(bools desc_nulls_first)*/ SELECT * FROM bools;

/*+IndexOnlyScan(bools asc_nulls_last)*/ SELECT FROM bools;
/*+IndexOnlyScan(bools asc_nulls_last)*/ SELECT count(*) FROM bools;

/*+IndexScan(bools)*/ EXPLAIN (COSTS OFF) SELECT b FROM bools WHERE b;
/*+IndexScan(bools)*/ SELECT b FROM bools WHERE b;

CREATE TABLE boolpart (a bool) PARTITION BY LIST (a);
CREATE TABLE boolpart_default PARTITION OF boolpart DEFAULT;
CREATE TABLE boolpart_t PARTITION OF boolpart FOR VALUES IN ('true');
CREATE TABLE boolpart_f PARTITION OF boolpart FOR VALUES IN ('false');
INSERT INTO boolpart (a) VALUES
    (true),
    (false),
    (true),
    (false),
    (NULL);
EXPLAIN (COSTS OFF) SELECT * FROM boolpart WHERE a = false;
SELECT * FROM boolpart WHERE a = false;
DROP TABLE boolpart;
