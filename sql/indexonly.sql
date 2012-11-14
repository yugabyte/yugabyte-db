LOAD 'pg_hint_plan';
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;

--
-- IndexOnlyScan hint test
--

-- 1.ヒントつきクエリとの比較用
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

-- 2.テーブルを指定、1と比較
/*+IndexOnlyScan(ti1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE c2 < 1000;

-- 3.エイリアスを指定、1と比較
/*+IndexOnlyScan(t_1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

-- 4.インデックスを指定、1と比較
/*+IndexOnlyScan(ti1_i2)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE c2 < 1000;

-- 5.エイリアスとインデックスを指定・指定したインデックスを使用、2と比較
/*+IndexOnlyScan(t_1 ti1_i2)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

-- 6.エイリアス1つとインデックス2つを指定、2つ目のインデックスを使用、5と比較
/*+IndexOnlyScan(t_1 ti1_i2 ti1_i1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

-- 7.Index ScanとIndex Only Scanの両方で使用しないようなインデックスを指定、
--   5と比較
/*+IndexOnlyScan(t_1 ti1_pred)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

-- 8.存在しないインデックスを指定
/*+IndexOnlyScan(t_1 ti1_i)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

--
-- IndexScan hint test
--

-- 1.IndexScanヒントが今までどおり使えることの確認
/*+IndexScan(t_1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 1000;

--
-- NoIndexOnlyScan hint test
--

-- 1.ヒントつきクエリとの比較用
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 10;

-- 2.テーブルを指定、1と比較
/*+NoIndexOnlyScan(ti1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE c2 < 10;

-- 3.エイリアスを指定、1と比較
/*+NoIndexOnlyScan(t_1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 10;

-- 4.インデックスを指定、1と比較
/*+NoIndexOnlyScan(ti1_i2)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 WHERE c2 < 10;

-- 5.エイリアスとインデックスを指定、2と比較
/*+NoIndexOnlyScan(t_1 ti1_i1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 10;

--
-- NoIndexScan hint test
--

-- 1.Index ScanだけでなIndex Only Scanも選択されないことの確認
/*+NoIndexScan(t_1)*/
EXPLAIN (COSTS false) SELECT c2 FROM s1.ti1 t_1 WHERE c2 < 10;

