-- hash pk
explain (costs off, summary off)
select * from t1 join t2 on t1.a = t2.k1;

-- unique secondary hash index
explain (costs off, summary off)
select * from t1 join t2 on t1.a = t2.k2;

-- non-unique secondary hash index
explain (costs off, summary off)
select * from t1 join t2 on t1.a = t2.k3;

-- unique secondary hash index partial key match
explain (costs off, summary off)
select * from t1 join t2 on t1.a = t2.k4;

-- pk
explain (costs off, summary off)
select * from t1 join t2r t2 on t1.a = t2.k1;

-- unique secondary index
explain (costs off, summary off)
select * from t1 join t2r t2 on t1.a = t2.k2;

-- non-unique secondary index
explain (costs off, summary off)
select * from t1 join t2r t2 on t1.a = t2.k3;

-- unique secondary index partial key match
explain (costs off, summary off)
select * from t1 join t2r t2 on t1.a = t2.k4;
