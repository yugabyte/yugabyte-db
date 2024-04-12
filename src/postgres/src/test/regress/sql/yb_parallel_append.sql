create table t(k bigint primary key, v text);
create table t1(k bigint primary key, v text);
create table t2(k bigint primary key, v text);
create table lp (a char) partition by list (a);
create table lp_default partition of lp default;
create table lp_ef partition of lp for values in ('e', 'f');
create table lp_ad partition of lp for values in ('a', 'd');
create table lp_bc partition of lp for values in ('b', 'c');
create table lp_null partition of lp for values in (null);

insert into t values (2, 'value_t_2'), (3, 'value_t_3');
insert into t1 values (1, 'value_t1_1'), (2, 'value_t1_2');
insert into t2 values (3, 'value_t2_3'), (4, 'value_t2_4');

set yb_enable_base_scans_cost_model to true;
set yb_enable_parallel_append to false;
set parallel_setup_cost to 0;
set parallel_tuple_cost to 0;

-- simple union
explain (costs off)
select * from t1 union select * from t2;
select * from t1 union select * from t2;

-- simple union all
explain (costs off)
select * from t1 union all select * from t2;
select * from t1 union all select * from t2;

-- joins union
explain (costs off)
select * from t, t1 where t.k = t1.k union select * from t, t2 where t.k = t2.k;
select * from t, t1 where t.k = t1.k union select * from t, t2 where t.k = t2.k;

-- joins union all
explain (costs off)
select * from t, t1 where t.k = t1.k union all select * from t, t2 where t.k = t2.k;
select * from t, t1 where t.k = t1.k union all select * from t, t2 where t.k = t2.k;

-- partitioned table
explain (costs off)
select * from lp;
select * from lp;

set yb_enable_parallel_append to true;

-- do not execute those parallel queries, there are known issues with YB PA
explain (costs off)
select * from t1 union select * from t2;

-- simple union all
explain (costs off)
select * from t1 union all select * from t2;

-- joins union
explain (costs off)
select * from t, t1 where t.k = t1.k union select * from t, t2 where t.k = t2.k;

-- joins union all
explain (costs off)
select * from t, t1 where t.k = t1.k union all select * from t, t2 where t.k = t2.k;

-- partitioned table
explain (costs off)
select * from lp;

