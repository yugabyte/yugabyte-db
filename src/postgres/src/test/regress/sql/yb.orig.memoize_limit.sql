-- A LIMIT above a join stops the outer scan early, so a Memoize on the inner
-- side gets few calls and few cache hits.
set client_min_messages to 'warning';
drop schema if exists memoize_limit cascade;
create schema memoize_limit;
set search_path to memoize_limit;
set yb_enable_cbo = on;

create table mc (id int, name text, primary key (id asc));
create table mo (id int, cid int, ts int, primary key (id asc));
create index nonconcurrently mo_ts on mo (ts asc);
insert into mc select i, 'c' || i from generate_series(1, 500) i;
insert into mo select i, (i::bigint * 7919) % 500 + 1, i from generate_series(1, 20000) i;
analyze mc;
analyze mo;

-- The outer path returns rows in the ORDER BY order: the join stops early.
explain (costs off)
select o.id, c.name from mo o join mc c on c.id = o.cid
where o.ts >= 2000 order by o.ts limit 50;

-- No ORDER BY: every outer path stops early.
explain (costs off)
select o.id, c.name from mo o join mc c on c.id = o.cid
where o.ts >= 2000 limit 50;

-- The outer path does not return rows in the ORDER BY order: the join reads
-- every outer row it needs to sort.
explain (costs off)
select o.id, c.name from mo o join mc c on c.id = o.cid
where o.ts >= 2000 order by o.id desc, c.name limit 50;

-- No LIMIT.
explain (costs off)
select o.id, c.name from mo o join mc c on c.id = o.cid
where o.ts >= 2000 order by o.ts;

-- Parallel plans.
set yb_test_force_parallel = force;
set max_parallel_workers_per_gather = 4;
explain (costs off)
select o.id, c.name from mo o join mc c on c.id = o.cid
where o.ts >= 2000 order by o.ts limit 50;
explain (costs off)
select o.id, c.name from mo o join mc c on c.id = o.cid
where o.ts >= 2000 limit 50;
reset yb_test_force_parallel;
reset max_parallel_workers_per_gather;

drop schema memoize_limit cascade;
