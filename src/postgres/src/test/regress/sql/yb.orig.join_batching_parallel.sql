--
-- Test parallel BNL plan correctness
--

-- Create colocated tables so we can test parallel plans, too.
\c yugabyte
set client_min_messages = 'warning';
drop database if exists colocateddb with (force);
create database colocateddb with colocation = on;
\c colocateddb

create table r (a int, x char(10), data text, primary key(a))
with (colocation = on);

create table s (b int, data text, primary key(b))
with (colocation = on);

create table t (a int, b int, data text, primary key(a, b))
with (colocation = on);

create table u (a int, b int, c int, data text, primary key(a, b, c))
with (colocation = on);

insert into r
  select i, rpad((i%2)::bpchar||(i%3)::bpchar, 10, '*'), lpad(i::bpchar, 5000, '#')::text
  from generate_series(1, 100) i;

insert into s select i, lpad(i::bpchar, 5000, '#')::text from generate_series(1, 200) i;

insert into t
  select r.a, s.b, lpad((r.a+s.b)::bpchar, 5000, '#')::text from r, s
    where ((r.a + s.b * 5000) % (200000/10000)) = 0;

insert into u
  select i / 10, i / 10, i, lpad(i::text, 5000, '#')::text from generate_series(1, 1000) i;

analyze r, s, t, u;


-- Force BNL by default
SET enable_hashjoin = off;
SET enable_mergejoin = off;
SET enable_seqscan = off;
SET enable_material = off;
SET yb_prefer_bnl = on;
SET enable_nestloop = off;

SET yb_bnl_batch_size = 3;


set yb_enable_cbo = on;


set yb_parallel_range_rows = 10;
set yb_parallel_range_size = '1MB';

-- Make serial plans look more expensive
set yb_fetch_row_limit = 0;
set yb_fetch_size_limit = '1GB';

-- Workaround for https://github.com/yugabyte/yugabyte-db/issues/28510
set join_collapse_limit=1;


--
-- #28112: BNL joining `s` and `u` must not have mixture of batched and
-- unbatched variables from `r` and `t`.
--
-- Note: join condition push down to the outer relation of inner BNL in
-- cascaded BNL setup not supported yet:
-- https://github.com/yugabyte/yugabyte-db/issues/28847
--

-- Parallel plan
/*+
  Leading(((r t) (s u)))
  IndexScan(r)
  IndexScan(s)
  IndexScan(t)
  IndexScan(u)
  YbBatchedNL(r t)
  YbBatchedNL(s u)
  YbBatchedNL(r s t u)
*/
explain (costs off)
select t.a, s.b, u.c
from
  (r join t on r.a = t.a)
  join (s join u on s.b = u.b) on s.b = t.b and r.a = u.a
where r.x like '%0%';

-- Save the parallel execution results
create temporary table bnl_px (a int, b int, c int);
/*+
  Leading(((r t) (s u)))
  IndexScan(r)
  IndexScan(s)
  IndexScan(t)
  IndexScan(u)
  YbBatchedNL(r t)
  YbBatchedNL(s u)
  YbBatchedNL(r s t u)
*/
insert into bnl_px
select t.a, s.b, u.c
from
  (r join t on r.a = t.a)
  join (s join u on s.b = u.b) on s.b = t.b and r.a = u.a
where r.x like '%0%';

-- Check stats of the results
select count(*) nrows,
    count(a) a_cnt, count(distinct a) a_ndv, min(a) a_min, max(a) a_max,
    count(b) b_cnt, count(distinct b) b_ndv, min(b) b_min, max(b) b_max,
    count(c) c_cnt, count(distinct c) c_ndv, min(c) c_min, max(c) c_max
  from bnl_px;


-- Serial plan

set max_parallel_workers = 0;
set max_parallel_workers_per_gather = 0;

/*+
  Leading(((r t) (s u)))
  IndexScan(r)
  IndexScan(s)
  IndexScan(t)
  IndexScan(u)
  YbBatchedNL(r t)
  YbBatchedNL(s u)
  YbBatchedNL(r s t u)
*/
explain (costs off)
select t.a, s.b, u.c
from
  (r join t on r.a = t.a)
  join (s join u on s.b = u.b) on s.b = t.b and r.a = u.a
where r.x like '%0%';

-- Save the serial execution results
create temporary table bnl_sx (a int, b int, c int);
/*+
  Leading(((r t) (s u)))
  IndexScan(r)
  IndexScan(s)
  IndexScan(t)
  IndexScan(u)
  YbBatchedNL(r t)
  YbBatchedNL(s u)
  YbBatchedNL(r s t u)
*/
insert into bnl_sx
select t.a, s.b, u.c
from
  (r join t on r.a = t.a)
  join (s join u on s.b = u.b) on s.b = t.b and r.a = u.a
where r.x like '%0%';

reset max_parallel_workers;
reset max_parallel_workers_per_gather;


-- Cross-check the results
select * from bnl_px
except
select * from bnl_sx;

select * from bnl_sx
except
select * from bnl_px;


-- Compare against non-BNL plan results
reset all;
set yb_enable_cbo = on;
set yb_bnl_batch_size = 1;

explain (costs off)
select t.a, s.b, u.c
from
  (r join t on r.a = t.a)
  join (s join u on s.b = u.b) on s.b = t.b and r.a = u.a
where r.x like '%0%';

create temporary table nl (a int, b int, c int);

insert into nl
select t.a, s.b, u.c
from
  (r join t on r.a = t.a)
  join (s join u on s.b = u.b) on s.b = t.b and r.a = u.a
where r.x like '%0%';

select * from nl
except
select * from bnl_sx;

select * from bnl_sx
except
select * from nl;
