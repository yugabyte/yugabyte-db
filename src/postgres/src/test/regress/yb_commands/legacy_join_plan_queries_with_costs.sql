explain (costs on, summary off)
select * from t1 join t2 on t2.k1 = t1.id join t2 t3 on t3.k3 = t1.id
where t3.k4 in (select t5.k2 from t1 t4 join t2r t5 on t4.id = t5.k3 and t4.id = 222)
 and t2.v = false and t3.k2 <> 123;
