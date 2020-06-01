-- Test primary key ordering
CREATE TABLE pk_asc(k int, v int, PRIMARY KEY(k ASC));
INSERT INTO pk_asc VALUES (20, 2),(30, 3),(10, 1);
SELECT * FROM pk_asc;
EXPLAIN (COSTS OFF) SELECT * FROM pk_asc ORDER BY k;
SELECT * FROM pk_asc ORDER BY k;
EXPLAIN (COSTS OFF) SELECT * FROM pk_asc ORDER BY k DESC;
SELECT * FROM pk_asc ORDER BY k DESC;
EXPLAIN (COSTS OFF) SELECT * FROM pk_asc ORDER BY k NULLS FIRST;
SELECT * FROM pk_asc ORDER BY k NULLS FIRST;

CREATE TABLE pk_desc(k int, v int, PRIMARY KEY(k DESC NULLS LAST));
INSERT INTO pk_desc VALUES (20, 12),(30, 13),(10, 11);
SELECT * FROM pk_desc;
EXPLAIN (COSTS OFF) SELECT * FROM pk_desc ORDER BY k;
SELECT * FROM pk_desc ORDER BY k;
EXPLAIN (COSTS OFF) SELECT * FROM pk_desc ORDER BY k DESC;
SELECT * FROM pk_desc ORDER BY k DESC;
EXPLAIN (COSTS OFF) SELECT * FROM pk_desc ORDER BY k NULLS FIRST;
SELECT * FROM pk_desc ORDER BY k NULLS FIRST;

CREATE TABLE  pk_multi(h int, r int, v text, PRIMARY KEY(h, r DESC));
INSERT INTO pk_multi(h, r, v) VALUES (1, 0, '1-0'),(1, 1, '1-1'),(1, 2, '1-2'),(1, 3, '1-3');
EXPLAIN (COSTS OFF) SELECT * FROM pk_multi WHERE h = 1;
SELECT * FROM pk_multi WHERE h = 1;

-- Test unique secondary index ordering
CREATE TABLE usc_asc(k int, v int);
CREATE UNIQUE INDEX ON usc_asc(v ASC NULLS FIRST);
INSERT INTO usc_asc VALUES (44, NULL),(22, 20),(33, 30),(11, 10),(44, NULL);
EXPLAIN (COSTS OFF) SELECT * FROM usc_asc ORDER BY v;
SELECT * FROM usc_asc ORDER BY v;
EXPLAIN (COSTS OFF) SELECT * FROM usc_asc ORDER BY v DESC NULLS LAST;
SELECT * FROM usc_asc ORDER BY v DESC NULLS LAST;
EXPLAIN (COSTS OFF) SELECT * FROM usc_asc ORDER BY v NULLS FIRST;
SELECT * FROM usc_asc ORDER BY v NULLS FIRST;

CREATE TABLE usc_multi_asc(k int, r int, v int);
CREATE INDEX ON usc_multi_asc(k, r ASC NULLS FIRST);
INSERT INTO usc_multi_asc(k, r, v) VALUES (1, 10, 1),(1, NULL, 2),(1, 20, 3);
EXPLAIN (COSTS OFF) SELECT * FROM usc_multi_asc WHERE k = 1;
SELECT * FROM usc_multi_asc WHERE k = 1;

-- Test non-unique secondary index ordering
CREATE TABLE sc_desc(k int, v int);
CREATE INDEX ON sc_desc(v DESC NULLS LAST);
INSERT INTO sc_desc VALUES (4, NULL),(2, 20),(3, 30),(1, 10),(4, NULL);
EXPLAIN (COSTS OFF) SELECT * FROM sc_desc ORDER BY v;
SELECT * FROM sc_desc ORDER BY v;
EXPLAIN (COSTS OFF) SELECT * FROM sc_desc ORDER BY v DESC NULLS LAST;
SELECT * FROM sc_desc ORDER BY v DESC NULLS LAST;
EXPLAIN (COSTS OFF) SELECT * FROM sc_desc ORDER BY v NULLS FIRST;
SELECT * FROM sc_desc ORDER BY v NULLS FIRST;

CREATE TABLE sc_multi_desc(k int, r int, v int);
CREATE INDEX ON sc_multi_desc(k, r DESC);
INSERT INTO sc_multi_desc(k, r, v) VALUES (1, 10, 10),(1, 10, 10),(1, NULL, 2),(1, 20, 3);
EXPLAIN (COSTS OFF) SELECT * FROM sc_multi_desc WHERE k = 1;
SELECT * FROM sc_multi_desc WHERE k = 1;

-- Test NULLS last ordering.
CREATE TABLE sc_desc_nl(h int, r int, v int);
CREATE INDEX on sc_desc_nl(h HASH, r DESC NULLS LAST);
INSERT INTO sc_desc_nl(h,r,v) values (1,1,1), (1,2,2), (1,3,3), (1,4,4), (1,5,5), (1, null, 6);
-- Rows should be ordered DESC NULLS LAST by r.
SELECT * FROM sc_desc_nl WHERE h = 1;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r >= 2;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r < 4;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r > 1 AND r <= 4;

-- <value> >/>=/=/<=/< null is never true per SQL semantics.
SELECT * FROM sc_desc_nl WHERE h = 1 AND r = null;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r >= null;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r > null;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r <= null;
SELECT * FROM sc_desc_nl WHERE h = 1 AND r < null;

-- IS NULL should be pushed down and return the expected result.
SELECT * FROM sc_desc_nl WHERE h = 1 AND r IS null;
EXPLAIN (COSTS OFF) SELECT * FROM sc_desc_nl WHERE h = 1 AND r IS null;

DROP TABLE sc_desc_nl;
