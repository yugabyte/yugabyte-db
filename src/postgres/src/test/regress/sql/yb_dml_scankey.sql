--
-- Test index scan using both hash code and row comparison filter.
--
CREATE TABLE hashcode_rowfilter (i int, j int, k int);
CREATE INDEX ON hashcode_rowfilter (k, j, i);
INSERT INTO hashcode_rowfilter VALUES (2, 3, 4), (1, 2, 4), (3, 4, 5);
\set filter 'yb_hash_code(k) = yb_hash_code(4) AND row(j, i) > row(2, 3)'
/*+IndexScan(hashcode_rowfilter)*/
SELECT * FROM hashcode_rowfilter WHERE :filter;
/*+IndexOnlyScan(hashcode_rowfilter)*/
SELECT k FROM hashcode_rowfilter WHERE :filter;

SELECT * FROM hashcode_rowfilter
         WHERE yb_hash_code(row(j, i)) = yb_hash_code(row(2, 1));

\set explain 'EXPLAIN (costs off)'
\set filter 'row(i, yb_hash_code(k)) > row(1, yb_hash_code(4))'
\set query 'SELECT * FROM hashcode_rowfilter WHERE :filter ORDER BY j'
\set is '/*+IndexScan(hashcode_rowfilter)*/'
-- TODO(#18347): fix output.
:explain :query; :explain :is :query; :query; :is :query;

--
-- Test index scan where filter is too large.
--
CREATE TABLE large_filter (i int);
CREATE INDEX ON large_filter (i ASC);
INSERT INTO large_filter VALUES (0), (1);
\set ten 'i>0 AND i>0 AND i>0 AND i>0 AND i>0 AND i>0 AND i>0 AND i>0 AND i>0 AND i>0'
\set sixty ':ten AND :ten AND :ten AND :ten AND :ten AND :ten'
-- 64 scan keys:
\set filter ':sixty AND i>0 AND i>0 AND i>0 AND i>0'
\set query 'SELECT i FROM large_filter WHERE :filter'
:explain :query; :query;
-- 65 scan keys:
\set filter ':sixty AND i>0 AND i>0 AND i>0 AND i>0 AND i>0'
:explain :query; :query;

--
-- Test index scan where row comparison filter is too large.
--
\set ten0 '0,0,0,0,0,0,0,0,0,0'
\set teni 'i,i,i,i,i,i,i,i,i,i'
\set sixty0 ':ten0,:ten0,:ten0,:ten0,:ten0,:ten0'
\set sixtyi ':teni,:teni,:teni,:teni,:teni,:teni'
-- 64 scan keys (1 header + 63 arguments):
\set filter 'row(:sixtyi,i,i,i) > row(:sixty0,0,0,0)'
:explain :query; :query;
-- 65 scan keys (1 header + 64 arguments):
\set filter 'row(:sixtyi,i,i,i,i) > row(:sixty0,0,0,0,0)'
:explain :query; :query;

--
-- Hash code filter should not count towards the limit.
--
\set query 'SELECT * FROM hashcode_rowfilter WHERE :filter ORDER BY j'
\set filter 'row(:sixtyi,i,i,i) > row(:sixty0,0,0,0) AND yb_hash_code(k) = yb_hash_code(4)'
:explain :is :query; :is :query;
-- TODO(#18347): fix output.
\set filter 'row(:sixtyi,i,i,i,yb_hash_code(k)) >= row(:sixty0,0,0,0,0)'
:explain :is :query; :is :query;

--
-- Hash code filter should have no limit.
--
\set one 'yb_hash_code(k) >= 0'
\set two ':one AND :one'
\set ten ':two AND :two AND :two AND :two AND :two'
\set sixty ':ten AND :ten AND :ten AND :ten AND :ten AND :ten'
-- 70 hash code scan keys:
\set filter ':sixty AND :ten'
\set query 'SELECT * FROM hashcode_rowfilter WHERE :filter ORDER BY i'
:explain :is :query; :is :query;
-- TODO(#18360): fix output.
\set one 'yb_hash_code(k) > -1'
:explain :is :query; :is :query;
