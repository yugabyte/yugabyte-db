-- test old extension version entry points

DROP EXTENSION pageinspect;
CREATE EXTENSION pageinspect VERSION '1.8';

CREATE TABLE test1 (a int8, b text);
INSERT INTO test1 VALUES (72057594037927937, 'text');
CREATE INDEX test1_a_idx ON test1 USING btree (a);

-- from page.sql
SELECT octet_length(get_raw_page('test1', 0)) AS main_0;
SELECT octet_length(get_raw_page('test1', 'main', 0)) AS main_0;
SELECT page_checksum(get_raw_page('test1', 0), 0) IS NOT NULL AS silly_checksum_test;

-- from btree.sql
SELECT * FROM bt_page_stats('test1_a_idx', 1);
SELECT * FROM bt_page_items('test1_a_idx', 1);

-- page_header() uses int instead of smallint for lower, upper, special and
-- pagesize in pageinspect >= 1.10.
ALTER EXTENSION pageinspect UPDATE TO '1.9';
\df page_header
SELECT pagesize, version FROM page_header(get_raw_page('test1', 0));

DROP TABLE test1;
DROP EXTENSION pageinspect;
