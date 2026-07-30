\set pwd `pwd`

CREATE TABLE t(a INT);
INSERT INTO t SELECT g from generate_series(1,10) g;
SELECT count(*) FROM t;
SELECT count(*) FROM public.t;

-- Create schema `other`
CREATE SCHEMA other;
CREATE TABLE other.t(a INT);
INSERT INTO other.t SELECT g from generate_series(1,100) g;
SELECT count(*) FROM other.t;

-- Test fully qualified table name combinations
SELECT count(*) FROM public.t, other.t;
SELECT count(*) FROM t, other.t;
SELECT count(*) FROM t,t;

-- search_path ORDER matters.
SET search_path TO other, public;
SELECT count(*) FROM t;
SELECT count(*) FROM t, public.t;

-- No search_path
SET search_path TO '';
SELECT count(*) FROM t, other.t;
SELECT count(*) FROM public.t, other.t;

SELECT count(*) FROM public.read_csv(:'pwd' || '/data/web_page.csv');

-- Cleanup
DROP TABLE other.t;
DROP SCHEMA other;
RESET search_path;

drop table t;
