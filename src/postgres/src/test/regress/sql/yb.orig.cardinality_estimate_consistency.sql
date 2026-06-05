-- Test cardinality estimate consistency
-- DEPENDENCY: yb.orig.cardinality_estimate_setup

set client_min_messages to warning;

analyze r;
analyze s;

set yb_enable_cbo = on;
set yb_enable_bitmapscan = on;
set enable_bitmapscan = on;

--
-- Index Scan node row count estimates
--
set enable_seqscan = off;

drop function if exists non_pushable cascade;

create function non_pushable(v bigint)
returns bigint
language plpgsql immutable as
$$
begin
  return v + 1;
end;
$$;

drop table if exists queries;

create table queries (qid serial, query text, primary key (qid ASC));

insert into queries values
    (DEFAULT, $$select pk from r where pk <= 500$$),

    (DEFAULT, $$select pk from r where pk <= 500 and pk % 100 <= 33$$),

    (DEFAULT, $$select pk from r where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 500$$),

    (DEFAULT, $$select pk from r where b <= 500$$),

    (DEFAULT, $$select pk from r where b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$select pk from r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    (DEFAULT, $$select b from r where b <= 500$$),

    (DEFAULT, $$select b from r where b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$select b from r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    (DEFAULT, $$select pk from r where pk <= 500 or b <= 500$$),

    (DEFAULT, $$select pk from r where pk <= 500 and pk % 100 <= 33 or b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$select pk from r where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 50
         or b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where pk <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where pk <= 500 and pk % 100 <= 33$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where pk <= 500 and pk % 100 <= 33 and non_pushable(pk) <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where b <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select pk from r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select b from r where b <= 500$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select b from r where b <= 500 and b % 100 <= 33$$),

    (DEFAULT, $$/*+ BitmapScan(r) */
        select b from r where b <= 500 and b % 100 <= 33 and non_pushable(b) <= 500$$)
;

drop type if exists index_plan_rows cascade;

create type index_plan_rows as (
    "Node Type" text,
    "Index Name" text,
    "Index Cond" text,
    "Storage Index Filter" text,
    "Storage Filter" text,
    "Filter" text,
    "Plan Rows" bigint,
    "Total Cost" float8
);


select * from queries order by qid;

select
    "Index Cond",
    "Index Filter",
    "Filter",
    "Plan Rows",
    rank() over (
        partition by
            regexp_replace("Index Cond", 'pk', 'b'),
            regexp_replace("Index Filter", 'pk', 'b'),
            regexp_replace("Filter", 'pk', 'b')
        order by "Total Cost"
    ) cost_rank,
    "Node Type",
    "Index Name",
    qid,
    nid
from (
    select
        qid,
        row_number() over (partition by qid) nid,
        "Node Type",
        "Index Name",
        "Index Cond",
        coalesce("Storage Index Filter", "Storage Filter") as "Index Filter",
        "Filter",
        "Plan Rows",
        "Total Cost"
    from
      queries,
      lateral explain_query_json(query) js,
      lateral jsonb_path_query(
        js.explain_line::jsonb,
        'strict $.** ? (@."Node Type" like_regex "Index.* Scan")'
      ) pln,
      lateral jsonb_populate_record(null::index_plan_rows, pln) rec
) v
order by
    regexp_replace("Index Cond", 'pk', 'b'),
    regexp_replace("Index Filter", 'pk', 'b'),
    regexp_replace("Filter", 'pk', 'b'),
    "Plan Rows",
    cost_rank,
    "Index Name",
    "Node Type",
    qid,
    nid;


drop function non_pushable cascade;
drop type index_plan_rows cascade;
drop table queries;


--
-- parameterized filter condition in Bitmap Table Scan.
-- the selectivity should be close to DEFAULT_INEQ_SEL (0.3333333333333333).
--
select
  bts->'Node Type' bmts,
  bts->'Storage Filter' bmts_filter,
  round((bts->'Plan Rows')::text::numeric / (bts->'Plans'->0->'Plan Rows')::text::numeric, 2) sel
from
  explain_query_json($$/*+ Leading((s r)) NestLoop(s r) BitmapScan(r) Set(yb_bnl_batch_size 1) */select * from r, s where (a = x or b <= 300) and a + b >= y$$) js,
  lateral to_json(js.explain_line->0->'Plan'->'Plans'->1) bts;

explain (costs off)
/*+ Leading((s r)) NestLoop(s r) BitmapScan(r) Set(yb_bnl_batch_size 1) */select * from r, s where (a = x or b <= 300) and a + b >= y;
