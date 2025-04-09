-- Test row count estimates

set client_min_messages = 'warning';

create function explain_query_json(query_sql text)
returns table (explain_line json)
language plpgsql as
$$
begin
  return query execute 'EXPLAIN (FORMAT json) ' || query_sql;
end;
$$;


-- create and populate the test tables with uniformly distributed values
-- to make the estimates predictable.
drop table if exists r, s;
create table r (a int, b int);
create table s (x int, y int);
create index i_r_a on r (a asc);
create index i_r_b on r (b asc);

insert into r
  select i / 5, i from generate_series(1, 12345) i;

insert into s
  select i / 3, i from generate_series(1, 123) i;

analyze r, s;


-- parameterized filter condition in Bitmap Table Scan.
-- the selectivity should be close to DEFAULT_INEQ_SEL (0.3333333333333333).
set yb_enable_base_scans_cost_model = on;
set yb_enable_bitmapscan = on;
set enable_bitmapscan = on;
set yb_prefer_bnl = off;

select
  bts->'Node Type' bmts,
  bts->'Storage Filter' bmts_filter,
  round((bts->'Plan Rows')::text::numeric / (bts->'Plans'->0->'Plan Rows')::text::numeric, 2) sel
from
  explain_query_json($$/*+ Leading((s r)) NestLoop(s r) BitmapScan(r) */select * from r, s where (a = x or b <= 300) and a + b >= y$$) js,
  lateral to_json(
    js.explain_line->0->'Plan'->'Plans'->1
  ) bts;
explain (costs off)
/*+ Leading((s r)) NestLoop(s r) BitmapScan(r) */select * from r, s where (a = x or b <= 300) and a + b >= y;
