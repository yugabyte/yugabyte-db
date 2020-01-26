--
-- PARTITION_JOIN
-- Test partitionwise join between partitioned tables
--

-- Enable partitionwise join, which by default is disabled.
SET enable_partitionwise_join to true;

--
-- partitioned by a single column
--
CREATE TABLE prt1 (a int, b int, c varchar) PARTITION BY RANGE(a);
CREATE TABLE prt1_p1 PARTITION OF prt1 FOR VALUES FROM (0) TO (250);
CREATE TABLE prt1_p3 PARTITION OF prt1 FOR VALUES FROM (500) TO (600);
CREATE TABLE prt1_p2 PARTITION OF prt1 FOR VALUES FROM (250) TO (500);
INSERT INTO prt1 SELECT i, i % 25, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 2 = 0;
CREATE INDEX iprt1_p1_a on prt1_p1(a);
CREATE INDEX iprt1_p2_a on prt1_p2(a);
CREATE INDEX iprt1_p3_a on prt1_p3(a);

CREATE TABLE prt2 (a int, b int, c varchar) PARTITION BY RANGE(b);
CREATE TABLE prt2_p1 PARTITION OF prt2 FOR VALUES FROM (0) TO (250);
CREATE TABLE prt2_p2 PARTITION OF prt2 FOR VALUES FROM (250) TO (500);
CREATE TABLE prt2_p3 PARTITION OF prt2 FOR VALUES FROM (500) TO (600);
INSERT INTO prt2 SELECT i % 25, i, to_char(i, 'FM0000') FROM generate_series(0, 599) i WHERE i % 3 = 0;
CREATE INDEX iprt2_p1_b on prt2_p1(b);
CREATE INDEX iprt2_p2_b on prt2_p2(b);
CREATE INDEX iprt2_p3_b on prt2_p3(b);

-- inner join
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;

-- left outer join, with whole-row reference; partitionwise join does not apply
SELECT t1, t2 FROM prt1 t1 LEFT JOIN prt2 t2 ON t1.a = t2.b WHERE t1.b = 0 ORDER BY t1.a, t2.b;

-- right outer join
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1 RIGHT JOIN prt2 t2 ON t1.a = t2.b WHERE t2.a = 0 ORDER BY t1.a, t2.b;

-- full outer join, with placeholder vars
SELECT t1.a, t1.c, t2.b, t2.c FROM (SELECT 50 phv, * FROM prt1 WHERE prt1.b = 0) t1 FULL JOIN (SELECT 75 phv, * FROM prt2 WHERE prt2.a = 0) t2 ON (t1.a = t2.b) WHERE t1.phv = t1.a OR t2.phv = t2.b ORDER BY t1.a, t2.b;

-- Join with pruned partitions from joining relations
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1 t1, prt2 t2 WHERE t1.a = t2.b AND t1.a < 450 AND t2.b > 250 AND t1.b = 0 ORDER BY t1.a, t2.b;

SELECT t1.a, t1.c, t2.b, t2.c FROM (SELECT * FROM prt1 WHERE a < 450) t1 LEFT JOIN (SELECT * FROM prt2 WHERE b > 250) t2 ON t1.a = t2.b WHERE t1.b = 0 ORDER BY t1.a, t2.b;

SELECT t1.a, t1.c, t2.b, t2.c FROM (SELECT * FROM prt1 WHERE a < 450) t1 FULL JOIN (SELECT * FROM prt2 WHERE b > 250) t2 ON t1.a = t2.b WHERE t1.b = 0 OR t2.a = 0 ORDER BY t1.a, t2.b;

-- Semi-join
SELECT t1.* FROM prt1 t1 WHERE t1.a IN (SELECT t2.b FROM prt2 t2 WHERE t2.a = 0) AND t1.b = 0 ORDER BY t1.a;

-- Anti-join with aggregates
SELECT sum(t1.a), avg(t1.a), sum(t1.b), avg(t1.b) FROM prt1 t1 WHERE NOT EXISTS (SELECT 1 FROM prt2 t2 WHERE t1.a = t2.b);

-- lateral reference
SELECT * FROM prt1 t1 LEFT JOIN LATERAL
			  (SELECT t2.a AS t2a, t3.a AS t3a, least(t1.a,t2.a,t3.b) FROM prt1 t2 JOIN prt2 t3 ON (t2.a = t3.b)) ss
			  ON t1.a = ss.t2a WHERE t1.b = 0 ORDER BY t1.a;

SELECT t1.a, ss.t2a, ss.t2c FROM prt1 t1 LEFT JOIN LATERAL
			  (SELECT t2.a AS t2a, t3.a AS t3a, t2.b t2b, t2.c t2c, least(t1.a,t2.a,t3.a) FROM prt1 t2 JOIN prt2 t3 ON (t2.a = t3.b)) ss
			  ON t1.c = ss.t2c WHERE (t1.b + coalesce(ss.t2b, 0)) = 0 ORDER BY t1.a;

--
-- partitioned by expression
--
CREATE TABLE prt1_e (a int, b int, c int) PARTITION BY RANGE(((a + b)/2));
CREATE TABLE prt1_e_p1 PARTITION OF prt1_e FOR VALUES FROM (0) TO (250);
CREATE TABLE prt1_e_p2 PARTITION OF prt1_e FOR VALUES FROM (250) TO (500);
CREATE TABLE prt1_e_p3 PARTITION OF prt1_e FOR VALUES FROM (500) TO (600);
INSERT INTO prt1_e SELECT i, i, i % 25 FROM generate_series(0, 599, 2) i;
CREATE INDEX iprt1_e_p1_ab2 on prt1_e_p1(((a+b)/2));
CREATE INDEX iprt1_e_p2_ab2 on prt1_e_p2(((a+b)/2));
CREATE INDEX iprt1_e_p3_ab2 on prt1_e_p3(((a+b)/2));

CREATE TABLE prt2_e (a int, b int, c int) PARTITION BY RANGE(((b + a)/2));
CREATE TABLE prt2_e_p1 PARTITION OF prt2_e FOR VALUES FROM (0) TO (250);
CREATE TABLE prt2_e_p2 PARTITION OF prt2_e FOR VALUES FROM (250) TO (500);
CREATE TABLE prt2_e_p3 PARTITION OF prt2_e FOR VALUES FROM (500) TO (600);
INSERT INTO prt2_e SELECT i, i, i % 25 FROM generate_series(0, 599, 3) i;

SELECT t1.a, t1.c, t2.b, t2.c FROM prt1_e t1, prt2_e t2 WHERE (t1.a + t1.b)/2 = (t2.b + t2.a)/2 AND t1.c = 0 ORDER BY t1.a, t2.b;

--
-- N-way join
--
SELECT t1.a, t1.c, t2.b, t2.c, t3.a + t3.b, t3.c FROM prt1 t1, prt2 t2, prt1_e t3 WHERE t1.a = t2.b AND t1.a = (t3.a + t3.b)/2 AND t1.b = 0 ORDER BY t1.a, t2.b;

SELECT t1.a, t1.c, t2.b, t2.c, t3.a + t3.b, t3.c FROM (prt1 t1 LEFT JOIN prt2 t2 ON t1.a = t2.b) LEFT JOIN prt1_e t3 ON (t1.a = (t3.a + t3.b)/2) WHERE t1.b = 0 ORDER BY t1.a, t2.b, t3.a + t3.b;

SELECT t1.a, t1.c, t2.b, t2.c, t3.a + t3.b, t3.c FROM (prt1 t1 LEFT JOIN prt2 t2 ON t1.a = t2.b) RIGHT JOIN prt1_e t3 ON (t1.a = (t3.a + t3.b)/2) WHERE t3.c = 0 ORDER BY t1.a, t2.b, t3.a + t3.b;

-- Cases with non-nullable expressions in subquery results;
-- make sure these go to null as expected
SELECT t1.a, t1.phv, t2.b, t2.phv, t3.a + t3.b, t3.phv FROM ((SELECT 50 phv, * FROM prt1 WHERE prt1.b = 0) t1 FULL JOIN (SELECT 75 phv, * FROM prt2 WHERE prt2.a = 0) t2 ON (t1.a = t2.b)) FULL JOIN (SELECT 50 phv, * FROM prt1_e WHERE prt1_e.c = 0) t3 ON (t1.a = (t3.a + t3.b)/2) WHERE t1.a = t1.phv OR t2.b = t2.phv OR (t3.a + t3.b)/2 = t3.phv ORDER BY t1.a, t2.b, t3.a + t3.b;

-- Semi-join
SELECT t1.* FROM prt1 t1 WHERE t1.a IN (SELECT t1.b FROM prt2 t1, prt1_e t2 WHERE t1.a = 0 AND t1.b = (t2.a + t2.b)/2) AND t1.b = 0 ORDER BY t1.a;

SELECT t1.* FROM prt1 t1 WHERE t1.a IN (SELECT t1.b FROM prt2 t1 WHERE t1.b IN (SELECT (t1.a + t1.b)/2 FROM prt1_e t1 WHERE t1.c = 0)) AND t1.b = 0 ORDER BY t1.a;

-- test merge joins
SET enable_hashjoin TO off;
SET enable_nestloop TO off;

SELECT t1.* FROM prt1 t1 WHERE t1.a IN (SELECT t1.b FROM prt2 t1 WHERE t1.b IN (SELECT (t1.a + t1.b)/2 FROM prt1_e t1 WHERE t1.c = 0)) AND t1.b = 0 ORDER BY t1.a;

SELECT t1.a, t1.c, t2.b, t2.c, t3.a + t3.b, t3.c FROM (prt1 t1 LEFT JOIN prt2 t2 ON t1.a = t2.b) RIGHT JOIN prt1_e t3 ON (t1.a = (t3.a + t3.b)/2) WHERE t3.c = 0 ORDER BY t1.a, t2.b, t3.a + t3.b;

-- MergeAppend on nullable column
SELECT t1.a, t2.b FROM (SELECT * FROM prt1 WHERE a < 450) t1 LEFT JOIN (SELECT * FROM prt2 WHERE b > 250) t2 ON t1.a = t2.b WHERE t1.b = 0 ORDER BY t1.a, t2.b;

-- merge join when expression with whole-row reference needs to be sorted;
-- partitionwise join does not apply
SELECT t1.a, t2.b FROM prt1 t1, prt2 t2 WHERE t1::text = t2::text AND t1.a = t2.b ORDER BY t1.a;

RESET enable_hashjoin;
RESET enable_nestloop;

--
-- partitioned by multiple columns
--
CREATE TABLE prt1_m (a int, b int, c int) PARTITION BY RANGE(a, ((a + b)/2));
CREATE TABLE prt1_m_p1 PARTITION OF prt1_m FOR VALUES FROM (0, 0) TO (250, 250);
CREATE TABLE prt1_m_p2 PARTITION OF prt1_m FOR VALUES FROM (250, 250) TO (500, 500);
CREATE TABLE prt1_m_p3 PARTITION OF prt1_m FOR VALUES FROM (500, 500) TO (600, 600);
INSERT INTO prt1_m SELECT i, i, i % 25 FROM generate_series(0, 599, 2) i;

CREATE TABLE prt2_m (a int, b int, c int) PARTITION BY RANGE(((b + a)/2), b);
CREATE TABLE prt2_m_p1 PARTITION OF prt2_m FOR VALUES FROM (0, 0) TO (250, 250);
CREATE TABLE prt2_m_p2 PARTITION OF prt2_m FOR VALUES FROM (250, 250) TO (500, 500);
CREATE TABLE prt2_m_p3 PARTITION OF prt2_m FOR VALUES FROM (500, 500) TO (600, 600);
INSERT INTO prt2_m SELECT i, i, i % 25 FROM generate_series(0, 599, 3) i;

SELECT t1.a, t1.c, t2.b, t2.c FROM (SELECT * FROM prt1_m WHERE prt1_m.c = 0) t1 FULL JOIN (SELECT * FROM prt2_m WHERE prt2_m.c = 0) t2 ON (t1.a = (t2.b + t2.a)/2 AND t2.b = (t1.a + t1.b)/2) ORDER BY t1.a, t2.b;

--
-- tests for list partitioned tables.
--
CREATE TABLE plt1 (a int, b int, c text) PARTITION BY LIST(c);
CREATE TABLE plt1_p1 PARTITION OF plt1 FOR VALUES IN ('0000', '0003', '0004', '0010');
CREATE TABLE plt1_p2 PARTITION OF plt1 FOR VALUES IN ('0001', '0005', '0002', '0009');
CREATE TABLE plt1_p3 PARTITION OF plt1 FOR VALUES IN ('0006', '0007', '0008', '0011');
INSERT INTO plt1 SELECT i, i, to_char(i/50, 'FM0000') FROM generate_series(0, 599, 2) i;

CREATE TABLE plt2 (a int, b int, c text) PARTITION BY LIST(c);
CREATE TABLE plt2_p1 PARTITION OF plt2 FOR VALUES IN ('0000', '0003', '0004', '0010');
CREATE TABLE plt2_p2 PARTITION OF plt2 FOR VALUES IN ('0001', '0005', '0002', '0009');
CREATE TABLE plt2_p3 PARTITION OF plt2 FOR VALUES IN ('0006', '0007', '0008', '0011');
INSERT INTO plt2 SELECT i, i, to_char(i/50, 'FM0000') FROM generate_series(0, 599, 3) i;

--
-- list partitioned by expression
--
CREATE TABLE plt1_e (a int, b int, c text) PARTITION BY LIST(ltrim(c, 'A'));
CREATE TABLE plt1_e_p1 PARTITION OF plt1_e FOR VALUES IN ('0000', '0003', '0004', '0010');
CREATE TABLE plt1_e_p2 PARTITION OF plt1_e FOR VALUES IN ('0001', '0005', '0002', '0009');
CREATE TABLE plt1_e_p3 PARTITION OF plt1_e FOR VALUES IN ('0006', '0007', '0008', '0011');
INSERT INTO plt1_e SELECT i, i, 'A' || to_char(i/50, 'FM0000') FROM generate_series(0, 599, 2) i;

-- test partition matching with N-way join
SELECT avg(t1.a), avg(t2.b), avg(t3.a + t3.b), t1.c, t2.c, t3.c FROM plt1 t1, plt2 t2, plt1_e t3 WHERE t1.b = t2.b AND t1.c = t2.c AND ltrim(t3.c, 'A') = t1.c GROUP BY t1.c, t2.c, t3.c ORDER BY t1.c, t2.c, t3.c;

--
-- tests for hash partitioned tables.
--
CREATE TABLE pht1 (a int, b int, c text) PARTITION BY HASH(c);
CREATE TABLE pht1_p1 PARTITION OF pht1 FOR VALUES WITH (MODULUS 3, REMAINDER 0);
CREATE TABLE pht1_p2 PARTITION OF pht1 FOR VALUES WITH (MODULUS 3, REMAINDER 1);
CREATE TABLE pht1_p3 PARTITION OF pht1 FOR VALUES WITH (MODULUS 3, REMAINDER 2);
INSERT INTO pht1 SELECT i, i, to_char(i/50, 'FM0000') FROM generate_series(0, 599, 2) i;

CREATE TABLE pht2 (a int, b int, c text) PARTITION BY HASH(c);
CREATE TABLE pht2_p1 PARTITION OF pht2 FOR VALUES WITH (MODULUS 3, REMAINDER 0);
CREATE TABLE pht2_p2 PARTITION OF pht2 FOR VALUES WITH (MODULUS 3, REMAINDER 1);
CREATE TABLE pht2_p3 PARTITION OF pht2 FOR VALUES WITH (MODULUS 3, REMAINDER 2);
INSERT INTO pht2 SELECT i, i, to_char(i/50, 'FM0000') FROM generate_series(0, 599, 3) i;

--
-- hash partitioned by expression
--
CREATE TABLE pht1_e (a int, b int, c text) PARTITION BY HASH(ltrim(c, 'A'));
CREATE TABLE pht1_e_p1 PARTITION OF pht1_e FOR VALUES WITH (MODULUS 3, REMAINDER 0);
CREATE TABLE pht1_e_p2 PARTITION OF pht1_e FOR VALUES WITH (MODULUS 3, REMAINDER 1);
CREATE TABLE pht1_e_p3 PARTITION OF pht1_e FOR VALUES WITH (MODULUS 3, REMAINDER 2);
INSERT INTO pht1_e SELECT i, i, 'A' || to_char(i/50, 'FM0000') FROM generate_series(0, 299, 2) i;

-- test partition matching with N-way join
SELECT avg(t1.a), avg(t2.b), avg(t3.a + t3.b), t1.c, t2.c, t3.c FROM pht1 t1, pht2 t2, pht1_e t3 WHERE t1.b = t2.b AND t1.c = t2.c AND ltrim(t3.c, 'A') = t1.c GROUP BY t1.c, t2.c, t3.c ORDER BY t1.c, t2.c, t3.c;

--
-- multiple levels of partitioning
--
CREATE TABLE prt1_l (a int, b int, c varchar) PARTITION BY RANGE(a);
CREATE TABLE prt1_l_p1 PARTITION OF prt1_l FOR VALUES FROM (0) TO (250);
CREATE TABLE prt1_l_p2 PARTITION OF prt1_l FOR VALUES FROM (250) TO (500) PARTITION BY LIST (c);
CREATE TABLE prt1_l_p2_p1 PARTITION OF prt1_l_p2 FOR VALUES IN ('0000', '0001');
CREATE TABLE prt1_l_p2_p2 PARTITION OF prt1_l_p2 FOR VALUES IN ('0002', '0003');
CREATE TABLE prt1_l_p3 PARTITION OF prt1_l FOR VALUES FROM (500) TO (600) PARTITION BY RANGE (b);
CREATE TABLE prt1_l_p3_p1 PARTITION OF prt1_l_p3 FOR VALUES FROM (0) TO (13);
CREATE TABLE prt1_l_p3_p2 PARTITION OF prt1_l_p3 FOR VALUES FROM (13) TO (25);
INSERT INTO prt1_l SELECT i, i % 25, to_char(i % 4, 'FM0000') FROM generate_series(0, 599, 2) i;

CREATE TABLE prt2_l (a int, b int, c varchar) PARTITION BY RANGE(b);
CREATE TABLE prt2_l_p1 PARTITION OF prt2_l FOR VALUES FROM (0) TO (250);
CREATE TABLE prt2_l_p2 PARTITION OF prt2_l FOR VALUES FROM (250) TO (500) PARTITION BY LIST (c);
CREATE TABLE prt2_l_p2_p1 PARTITION OF prt2_l_p2 FOR VALUES IN ('0000', '0001');
CREATE TABLE prt2_l_p2_p2 PARTITION OF prt2_l_p2 FOR VALUES IN ('0002', '0003');
CREATE TABLE prt2_l_p3 PARTITION OF prt2_l FOR VALUES FROM (500) TO (600) PARTITION BY RANGE (a);
CREATE TABLE prt2_l_p3_p1 PARTITION OF prt2_l_p3 FOR VALUES FROM (0) TO (13);
CREATE TABLE prt2_l_p3_p2 PARTITION OF prt2_l_p3 FOR VALUES FROM (13) TO (25);
INSERT INTO prt2_l SELECT i % 25, i, to_char(i % 4, 'FM0000') FROM generate_series(0, 599, 3) i;

-- inner join, qual covering only top-level partitions
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1_l t1, prt2_l t2 WHERE t1.a = t2.b AND t1.b = 0 ORDER BY t1.a, t2.b;

-- left join
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1_l t1 LEFT JOIN prt2_l t2 ON t1.a = t2.b AND t1.c = t2.c WHERE t1.b = 0 ORDER BY t1.a, t2.b;

-- right join
SELECT t1.a, t1.c, t2.b, t2.c FROM prt1_l t1 RIGHT JOIN prt2_l t2 ON t1.a = t2.b AND t1.c = t2.c WHERE t2.a = 0 ORDER BY t1.a, t2.b;

-- full join
SELECT t1.a, t1.c, t2.b, t2.c FROM (SELECT * FROM prt1_l WHERE prt1_l.b = 0) t1 FULL JOIN (SELECT * FROM prt2_l WHERE prt2_l.a = 0) t2 ON (t1.a = t2.b AND t1.c = t2.c) ORDER BY t1.a, t2.b;

-- lateral partitionwise join
SELECT * FROM prt1_l t1 LEFT JOIN LATERAL
			  (SELECT t2.a AS t2a, t2.c AS t2c, t2.b AS t2b, t3.b AS t3b, least(t1.a,t2.a,t3.b) FROM prt1_l t2 JOIN prt2_l t3 ON (t2.a = t3.b AND t2.c = t3.c)) ss
			  ON t1.a = ss.t2a AND t1.c = ss.t2c WHERE t1.b = 0 ORDER BY t1.a;
