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
