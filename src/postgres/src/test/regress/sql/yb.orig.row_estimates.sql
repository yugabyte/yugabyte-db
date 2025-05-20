-- Test row count estimates

set client_min_messages = 'warning';
drop function if exists check_estimated_rows, get_estimated_rows;

-- following plpgsql functions borrowed from vanilla pg tests

-- from gin.sql, removed guc settings and ANALYZE option
create function explain_query_json(query_sql text)
returns table (explain_line json)
language plpgsql as
$$
begin
  return query execute 'EXPLAIN (FORMAT json) ' || query_sql;
end;
$$;

-- from stats_ext.sql, removed the analyze option.
-- check the number of estimated/actual rows in the top node
create function check_estimated_rows(text) returns table (estimated int, actual int)
language plpgsql as
$$
declare
    ln text;
    tmp text[];
    first_row bool := true;
begin
    for ln in
        execute format('explain analyze %s', $1)
    loop
        if first_row then
            first_row := false;
            tmp := regexp_match(ln, 'rows=(\d*) .* rows=(\d*)');
            return query select tmp[1]::int, tmp[2]::int;
        end if;
    end loop;
end;
$$;

-- modified the above to return estimates only
create function get_estimated_rows(text) returns table (estimated int)
language plpgsql as
$$
declare
    ln text;
    tmp text[];
    first_row bool := true;
begin
    for ln in
        execute format('explain %s', $1)
    loop
        if first_row then
            first_row := false;
            tmp := regexp_match(ln, 'rows=(\d*)');
            return query select tmp[1]::int;
        end if;
    end loop;
end;
$$;


-- create and populate the test tables with uniformly distributed values
-- to make the estimates predictable.
drop table if exists r, s;
create table r (pk int, a int, b int, c char(10), d int, e int, v char(666), primary key (pk asc));
create table s (x int, y int, z char(10));
create index i_r_a on r (a asc);
create index i_r_a_ge_1k on r (a asc) where a >= 1000;
create unique index i_r_b on r (b asc);
create index i_r_c on r (c asc);

insert into r
  select
    i, i / 10, i,
    -- 'aaa', 'aab', 'aac', ...
    concat(chr((((i-1)/26/26) % 26) + ascii('a')),
           chr((((i-1)/26) % 26) + ascii('a')),
           chr(((i-1) % 26) + ascii('a'))),
    i, i / 10,
    sha512(('x'||i)::bytea)::bpchar||lpad(sha512((i||'y')::bytea)::bpchar, 536, '#')
  from generate_series(1, 12345) i;

insert into s
  select
    i / 3, i,
    concat(chr((((i-1)/26/26) % 26) + ascii('a')),
           chr((((i-1)/26) % 26) + ascii('a')),
           chr(((i-1) % 26) + ascii('a')))
  from generate_series(1, 123) i;


-- store test queries in a table
drop table if exists queries;
create table queries (id serial, hard boolean, query text, primary key (id asc));
insert into queries values
    (DEFAULT, false, 'select * from r'),
    (DEFAULT, false, 'select * from r where pk = 123'),
    (DEFAULT, false, 'select * from r where pk < 123'),
    (DEFAULT, false, 'select * from r where pk > 123'),
    (DEFAULT, false, 'select * from r where pk <= 123'),
    (DEFAULT, false, 'select * from r where pk >= 123'),
    (DEFAULT, false, 'select * from r where pk <> 123'),
    (DEFAULT, false, 'select * from r where pk between 123 and 456'),
    (DEFAULT, false, 'select * from r where a = 123'),
    (DEFAULT, false, 'select * from r where a < 123'),
    (DEFAULT, false, 'select * from r where a > 123'),
    (DEFAULT, false, 'select * from r where a <= 123'),
    (DEFAULT, false, 'select * from r where a >= 123'),
    (DEFAULT, false, 'select * from r where a <> 123'),
    (DEFAULT, false, 'select * from r where a between 123 and 456'),
    (DEFAULT, false, 'select * from r where b = 123'),
    (DEFAULT, false, 'select * from r where b < 123'),
    (DEFAULT, false, 'select * from r where b > 123'),
    (DEFAULT, false, 'select * from r where b <= 123'),
    (DEFAULT, false, 'select * from r where b >= 123'),
    (DEFAULT, false, 'select * from r where b <> 123'),
    (DEFAULT, false, 'select * from r where b between 123 and 456'),
    (DEFAULT, false, 'select * from r where d = 123'),
    (DEFAULT, false, 'select * from r where d < 123'),
    (DEFAULT, false, 'select * from r where d > 123'),
    (DEFAULT, false, 'select * from r where d <= 123'),
    (DEFAULT, false, 'select * from r where d >= 123'),
    (DEFAULT, false, 'select * from r where d <> 123'),
    (DEFAULT, false, 'select * from r where d between 123 and 456'),
    (DEFAULT, false, 'select * from r where e = 123'),
    (DEFAULT, false, 'select * from r where e < 123'),
    (DEFAULT, false, 'select * from r where e > 123'),
    (DEFAULT, false, 'select * from r where e <= 123'),
    (DEFAULT, false, 'select * from r where e >= 123'),
    (DEFAULT, false, 'select * from r where e <> 123'),
    (DEFAULT, false, 'select * from r where e between 123 and 456'),
    (DEFAULT, false, 'select * from r where c = ''abc'''),
    (DEFAULT, true, 'select * from r where c < ''ab'''),
    (DEFAULT, true, 'select * from r where c > ''ab'''),
    (DEFAULT, true, 'select * from r where c <= ''ab'''),
    (DEFAULT, true, 'select * from r where c >= ''ab'''),
    (DEFAULT, false, 'select * from r where c <> ''abc'''),
    (DEFAULT, true, 'select * from r where c between ''ab'' and ''bz'''),
    (DEFAULT, true, 'select * from r where c like ''ab%'''),
    (DEFAULT, true, 'select * from r where c not like ''ab%'''),
    (DEFAULT, false, 'select count(*) from r group by pk'),
    (DEFAULT, false, 'select count(*) from r group by a'),
    (DEFAULT, false, 'select count(*) from r group by b'),
    (DEFAULT, false, 'select count(*) from r group by c'),
    (DEFAULT, false, 'select count(*) from r group by d'),
    (DEFAULT, false, 'select count(*) from r group by e'),
    (DEFAULT, false, 'select distinct pk from r'),
    (DEFAULT, false, 'select distinct a from r'),
    (DEFAULT, false, 'select distinct b from r'),
    (DEFAULT, false, 'select distinct c from r'),
    (DEFAULT, false, 'select distinct d from r'),
    (DEFAULT, false, 'select distinct e from r'),
    (DEFAULT, true, 'select * from r, s where pk = x'),
    (DEFAULT, true, 'select * from r, s where pk = y'),
    (DEFAULT, true, 'select * from r, s where a = x'),
    (DEFAULT, true, 'select * from r, s where a = y'),
    (DEFAULT, true, 'select * from r, s where b = x'),
    (DEFAULT, true, 'select * from r, s where b = y'),
    (DEFAULT, true, 'select * from r, s where d = x'),
    (DEFAULT, true, 'select * from r, s where d = y'),
    (DEFAULT, true, 'select * from r, s where e = x'),
    (DEFAULT, true, 'select * from r, s where e = y'),
    (DEFAULT, true, 'select * from r, s where c = z'),
    (DEFAULT, true, 'select * from r, s where pk < x'),
    (DEFAULT, true, 'select * from r, s where pk < y'),
    (DEFAULT, true, 'select * from r, s where a < x'),
    (DEFAULT, true, 'select * from r, s where a < y'),
    (DEFAULT, true, 'select * from r, s where b < x'),
    (DEFAULT, true, 'select * from r, s where b < y'),
    (DEFAULT, true, 'select * from r, s where d < x'),
    (DEFAULT, true, 'select * from r, s where d < y'),
    (DEFAULT, true, 'select * from r, s where e < x'),
    (DEFAULT, true, 'select * from r, s where e < y'),
    (DEFAULT, true, 'select * from r, s where c < z'),
    (DEFAULT, true, 'select * from r, s where x < pk'),
    (DEFAULT, true, 'select * from r, s where y < pk'),
    (DEFAULT, true, 'select * from r, s where x < a'),
    (DEFAULT, true, 'select * from r, s where y < a'),
    (DEFAULT, true, 'select * from r, s where x < b'),
    (DEFAULT, true, 'select * from r, s where y < b'),
    (DEFAULT, true, 'select * from r, s where x < d'),
    (DEFAULT, true, 'select * from r, s where y < d'),
    (DEFAULT, true, 'select * from r, s where x < e'),
    (DEFAULT, true, 'select * from r, s where y < e'),
    (DEFAULT, true, 'select * from r, s where z < c'),
    (DEFAULT, false, 'select * from r where a = 10 and e = 10'),
    (DEFAULT, false, 'select 0 from r group by a, e'),
    (DEFAULT, false, 'select distinct a, e from r'),
    (DEFAULT, false, 'select * from r where b % 5 = 0'),
    (DEFAULT, false, 'select distinct b % 5 from r'),
    (DEFAULT, false, 'select 0 from r group by b % 5'),
    (DEFAULT, false, 'select 0 from r where a between 1000 and 1200');


--
-- check estimates with different yb_enable_cbo values
--

drop table if exists off_before_analyze, off_after_analyze;
drop table if exists on_before_analyze, on_after_analyze;
drop table if exists legacy_before_analyze, legacy_after_analyze;
drop table if exists legacy_stats_before_analyze, legacy_stats_after_analyze;
drop table if exists cbo_estimates;

-- save the estimates before analyze

set yb_enable_cbo = off;
create temporary table off_before_analyze as
  select id, query, get_estimated_rows(query) rows
    from queries q;

set yb_enable_cbo = on;
create temporary table on_before_analyze as
  select id, query, get_estimated_rows(query) rows
    from queries q;

set yb_enable_cbo = legacy_mode;
create temporary table legacy_before_analyze as
  select id, query, get_estimated_rows(query) rows
    from queries q;

set yb_enable_cbo = legacy_stats_mode;
create temporary table legacy_stats_before_analyze as
  select id, query, get_estimated_rows(query) rows
    from queries q;


-- create extended stats then analyze

create statistics r_a_e on a, e from r;
create statistics r_bmod5 on (b % 5) from r;
analyze r, s;


-- save the estimates after analyze

set yb_enable_cbo = off;
create temporary table off_after_analyze as
  select id, query, get_estimated_rows(query) rows
    from queries q;

set yb_enable_cbo = on;
create temporary table on_after_analyze as
  select id, query, get_estimated_rows(query) rows
    from queries q;

set yb_enable_cbo = legacy_mode;
create temporary table legacy_after_analyze as
  select id, query, get_estimated_rows(query) rows
    from queries q;

set yb_enable_cbo = legacy_stats_mode;
create temporary table legacy_stats_after_analyze as
  select id, query, get_estimated_rows(query) rows
    from queries q;


-- see how the estimates change before vs. after analyze

--- no predicate ---
-- off: default -> unchanged
select b.id, b.query, b.rows before, a.rows after
  from off_before_analyze b join off_after_analyze a using (id)
  where id = 1 order by id;

-- on, legacy, legacy_stats: default -> accurate
select b.id, b.query, b.rows before, a.rows after
  from on_before_analyze b join on_after_analyze a using (id)
  where id = 1 order by id;

select b.id, b.query, b.rows before, a.rows after
  from legacy_before_analyze b join legacy_after_analyze a using (id)
  where id = 1 order by id;

select b.id, b.query, b.rows before, a.rows after
  from legacy_stats_before_analyze b join legacy_stats_after_analyze a using (id)
  where id = 1 order by id;


--- equality ---
-- off:
--   unique indexed: 1 -> unchanged
--   non-unique indexed: 1% (YBC_SINGLE_KEY_SELECTIVITY) of 1000 -> unchanged
--   unindexed: 1000 (predicate ignored) -> unchanged
select b.id, b.query, b.rows before, a.rows after
  from off_before_analyze b join off_after_analyze a using (id)
  where b.query ~ 'p*[kabde] = 123' order by id;

-- on, legacy_stats:
--   unique indexed: 1 -> 1 (unchanged, accurate)
--   others (non-unique indexed & unindexed): 5% (DEFAULT_EQ_SEL) of 1000 -> accurate
select b.id, b.query, b.rows before, a.rows after
  from on_before_analyze b join on_after_analyze a using (id)
  where b.query ~ 'p*[kabde] = 123' order by id;

select b.id, b.query, b.rows before, a.rows after
  from legacy_stats_before_analyze b join legacy_stats_after_analyze a using (id)
  where b.query ~ 'p*[kabde] = 123' order by id;

-- legacy:
--   unique indexed: 1 -> 1 (unchanged, accurate)
--   non-unique indexed: 1% of 1000 -> 1% of 12345 (predicate ignored)
--   unindexed: 1000 -> 12345 (predicate ignored)
select b.id, b.query, b.rows before, a.rows after
  from legacy_before_analyze b join legacy_after_analyze a using (id)
  where b.query ~ 'p*[kabde] = 123' order by id;


--- group by ---
-- off:
--   unique indexed: 1000 -> unchanged
--   others: 200 (DEFAULT_NUM_DISTINCT) -> unchanged
select b.id, b.query, b.rows before, a.rows after
  from off_before_analyze b join off_after_analyze a using (id)
  where b.query ~ 'group by p*[kabde]$' order by id;

-- on, legacy_stats, legacy:
--   unique indexed: 1000 -> 1000 (unchanged, accurate)
--   others: 200 -> accurate
select b.id, b.query, b.rows before, a.rows after
  from on_before_analyze b join on_after_analyze a using (id)
  where b.query ~ 'group by p*[kabde]$' order by id;

select b.id, b.query, b.rows before, a.rows after
  from legacy_stats_before_analyze b join legacy_stats_after_analyze a using (id)
  where b.query ~ 'group by p*[kabde]$' order by id;

select b.id, b.query, b.rows before, a.rows after
  from legacy_before_analyze b join legacy_after_analyze a using (id)
  where b.query ~ 'group by p*[kabde]$' order by id;


--- equi-join ---
-- off:
--   unique indexed: 1000 -> unchanged
--   others: 1000 * 1000 * (1/200) -> unchanged
select b.id, b.query, b.rows before, a.rows after
  from off_before_analyze b join off_after_analyze a using (id)
  where b.query ~ 'where p*[kabde] = [xy]' order by id;

-- on, legacy_stats, legacy:
--   unique indexed: 1000 -> 123 (accurate)
--   others: 1000 * 1000 * (1/200) -> 123 or 1230 (accurate)
select b.id, b.query, b.rows before, a.rows after
  from on_before_analyze b join on_after_analyze a using (id)
  where b.query ~ 'where p*[kabde] = [xy]' order by id;

select b.id, b.query, b.rows before, a.rows after
  from legacy_stats_before_analyze b join legacy_stats_after_analyze a using (id)
  where b.query ~ 'where p*[kabde] = [xy]' order by id;

select b.id, b.query, b.rows before, a.rows after
  from legacy_before_analyze b join legacy_after_analyze a using (id)
  where b.query ~ 'where p*[kabde] = [xy]' order by id;


-- should return no row as the estimates match
select *
from legacy_before_analyze t1 join off_before_analyze t2 using (id)
where t1.rows <> t2.rows;

select *
from off_before_analyze t1 join off_after_analyze t2 using (id)
where t1.rows <> t2.rows;


--
-- check CBO estimates for easy queries
--

set yb_enable_cbo = on;

create temporary table cbo_estimates (
  id int,
  query text,
  rows int[]
);

insert into cbo_estimates
  select
    id, query,
    string_to_array(trim(both '()' from check_estimated_rows(query)::bpchar), ',')::int[]
  from queries where hard = false;

-- should return no row
select id, query, rows[1] estimated, rows[2] actual
from cbo_estimates
where abs(rows[1] - rows[2]) > 2
  and abs(rows[1] - rows[2])/least(rows[1], rows[2])::float > 0.005;


--
-- test selectivity estimates
--
analyze r, s;

set yb_enable_cbo = on;
set yb_enable_bitmapscan = on;
set enable_bitmapscan = on;

-- parameterized filter condition in Bitmap Table Scan.
-- the selectivity should be close to DEFAULT_INEQ_SEL (0.3333333333333333).
select
  bts->'Node Type' bmts,
  bts->'Storage Filter' bmts_filter,
  round((bts->'Plan Rows')::text::numeric / (bts->'Plans'->0->'Plan Rows')::text::numeric, 2) sel
from
  explain_query_json($$/*+ Leading((s r)) NestLoop(s r) BitmapScan(r) Set(yb_bnl_batch_size 1) */select * from r, s where (a = x or b <= 300) and a + b >= y$$) js,
  lateral to_json(js.explain_line->0->'Plan'->'Plans'->1) bts;

explain (costs off)
/*+ Leading((s r)) NestLoop(s r) BitmapScan(r) Set(yb_bnl_batch_size 1) */select * from r, s where (a = x or b <= 300) and a + b >= y;


drop table if exists r, s, queries;
