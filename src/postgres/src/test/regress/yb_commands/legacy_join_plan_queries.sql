--
-- m x n, 1 x n and semijoin on pk, unique secondary and non-unique secondary
-- indexed column.
--

-- hash pk
explain (costs off, summary off)
select * from t1 join t2 on t1.a = t2.k1;

explain (costs off, summary off)
select * from t1 join t2 on t1.id = 7 and t1.a = t2.k1;

explain (costs off, summary off)
select * from t1 where exists (select 0 from t2 where t1.a = t2.k1);


-- unique secondary hash index
explain (costs off, summary off)
select * from t1 join t2 on t1.a = t2.k2;

explain (costs off, summary off)
select * from t1 join t2 on t1.id = 7 and t1.a = t2.k2;

explain (costs off, summary off)
select * from t1 where exists (select 0 from t2 where t1.a = t2.k2);


-- non-unique secondary hash index
explain (costs off, summary off)
select * from t1 join t2 on t1.a = t2.k3;

explain (costs off, summary off)
select * from t1 join t2 on t1.id = 7 and t1.a = t2.k3;

explain (costs off, summary off)
select * from t1 where exists (select 0 from t2 where t1.a = t2.k3);


-- unique secondary hash index partial key match
explain (costs off, summary off)
select * from t1 join t2 on t1.a = t2.k4;

explain (costs off, summary off)
select * from t1 join t2 on t1.id = 7 and t1.a = t2.k4;

explain (costs off, summary off)
select * from t1 where exists (select 0 from t2 where t1.a = t2.k4);


-- pk
explain (costs off, summary off)
select * from t1 join t2r t2 on t1.a = t2.k1;

explain (costs off, summary off)
select * from t1 join t2r t2 on t1.id = 7 and t1.a = t2.k1;

explain (costs off, summary off)
select * from t1 where exists (select 0 from t2r t2 where t1.a = t2.k1);


-- unique secondary index
explain (costs off, summary off)
select * from t1 join t2r t2 on t1.a = t2.k2;

explain (costs off, summary off)
select * from t1 join t2r t2 on t1.id = 7 and t1.a = t2.k2;

explain (costs off, summary off)
select * from t1 where exists (select 0 from t2r t2 where t1.a = t2.k2);


-- non-unique secondary index
explain (costs off, summary off)
select * from t1 join t2r t2 on t1.a = t2.k3;

explain (costs off, summary off)
select * from t1 join t2r t2 on t1.id = 7 and t1.a = t2.k3;

explain (costs off, summary off)
select * from t1 where exists (select 0 from t2r t2 where t1.a = t2.k3);


-- unique secondary index partial key match
explain (costs off, summary off)
select * from t1 join t2r t2 on t1.a = t2.k4;

explain (costs off, summary off)
select * from t1 join t2r t2 on t1.id = 7 and t1.a = t2.k4;

explain (costs off, summary off)
select * from t1 where exists (select 0 from t2r t2 where t1.a = t2.k4);


-- a batchable equality on one outer relation, an inequality on another outer
-- relation in the same index, and a join filter referencing both: the mixed
-- condition cannot be applied to a batched scan
explain (costs off, summary off)
select * from t1, t1 t3, t2r t2 where t2.k4 = t1.a and t2.k5 <= t3.id and t2.k3 < t1.id + t3.a;
