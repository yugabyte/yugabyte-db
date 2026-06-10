-- Create database objects used by planner's roundtrip estimate tests
set client_min_messages to 'warning';

drop database if exists roundtrip_test with (force);
create database roundtrip_test with colocation = true;
alter database roundtrip_test set yb_explain_hide_non_deterministic_fields = off;
alter database roundtrip_test set yb_enable_cbo = on;

\c roundtrip_test
set client_min_messages to 'warning';

drop function if exists explain_query_json cascade;
create function explain_query_json(query_sql text, with_analyze bool)
returns table (explain_line json)
language plpgsql as
$$
begin
  return query execute
    case when with_analyze then
      'explain (format json, analyze, debug, dist, summary off) '
    else
      'explain (format json, summary off) '
    end
    || query_sql;
end;
$$;

-- non-pushable expression to force a local Filter
drop function if exists non_pushable cascade;
create function non_pushable(v bigint) returns bigint
language plpgsql immutable as
$$
begin
  return v + 1;
end;
$$;


drop type if exists index_plan_rows cascade;
create type index_plan_rows as (
    "Node Type" text,
    "Scan Direction" text,
    "Index Name" text,
    "Index Cond" text,
    "Storage Index Filter" text,
    "Storage Filter" text,
    "Filter" text,
    "Plan Rows" float8,
    "Total Cost" float8,
    "Estimated Table Roundtrips" float8,
    "Estimated Index Roundtrips" float8,
    "Storage Table Read Requests" float8,
    "Storage Index Read Requests" float8
);


-- Schema "ce" hosts cardinality_estimate-style tables (used by the
-- range, filter combination, IN-list and cardinality_estimate queries).
drop schema if exists ce cascade;
create schema ce;

create table ce.r (
    pk int,
    a int,
    b int,
    c char(10),
    d int,
    e int,
    bl bool,
    v char(666),
    primary key (pk asc)
) with (colocation = off);

create index nonconcurrently i_r_a on ce.r (a asc);
create unique index nonconcurrently i_r_b on ce.r (b asc);
create index nonconcurrently i_r_c on ce.r (c asc);
create index nonconcurrently i_r_bl on ce.r (bl asc);

insert into ce.r
    select
        i, ((i - 1) / 10) + 1, i,
        concat(chr((((i-1)/26/26) % 26) + ascii('a')),
               chr((((i-1)/26) % 26) + ascii('a')),
               chr(((i-1) % 26) + ascii('a'))),
        i, ((i - 1) / 10) + 1,
        i % 2 = 1,
        sha512(('x'||i)::bytea)::bpchar||lpad(sha512((i||'y')::bytea)::bpchar, 536, '#')
    from generate_series(1, 12345) i;

alter table ce.r alter column a set statistics 10000;
alter table ce.r alter column b set statistics 10000;
alter table ce.r alter column e set statistics 10000;

create table ce.rC (like ce.r including constraints including indexes including statistics)
    with (colocation = on);
insert into ce.rC select * from ce.r;

-- Keep cardinality-estimation noise out of the roundtrip-estimation checks.
-- The modulo predicates are deliberately close to default selectivity, but
-- their conjunction is correlated because e is derived from b.
create statistics ce_r_pk_mod100 on ((pk % 100)) from ce.r;
create statistics ce_r_b_mod100 on ((b % 100)) from ce.r;
create statistics ce_r_b_e_mod100 (mcv) on ((b % 100)), ((e % 100)) from ce.r;
alter statistics ce_r_pk_mod100 set statistics 10000;
alter statistics ce_r_b_mod100 set statistics 10000;
alter statistics ce_r_b_e_mod100 set statistics 10000;

create statistics ce_rc_pk_mod100 on ((pk % 100)) from ce.rC;
create statistics ce_rc_b_mod100 on ((b % 100)) from ce.rC;
create statistics ce_rc_b_e_mod100 (mcv) on ((b % 100)), ((e % 100)) from ce.rC;
alter statistics ce_rc_pk_mod100 set statistics 10000;
alter statistics ce_rc_b_mod100 set statistics 10000;
alter statistics ce_rc_b_e_mod100 set statistics 10000;

analyze ce.r;
analyze ce.rC;


-- Schema "sn" hosts seeknext_estimate-style tables (used by the
-- seeknext_estimate queries).
drop schema if exists sn cascade;
create schema sn;

create table sn.r (
    pk int,
    -- discrete uniform distribution
    f1 int,
    f2 int,
    f3 int,
    f4 int,
    f5 int,
    f10 int,
    f100 int,
    f1000 int,
    dv1 int,
    -- discrete normal distribution
    skw1 int,
    skw2 int,
    skw3 int,
    -- stub data
    v int,
    vv char(512),
    primary key (pk asc)
) with (colocation = off);

alter table sn.r alter column skw1 set statistics 500;
alter table sn.r alter column skw2 set statistics 500;
alter table sn.r alter column skw3 set statistics 500;

create index nonconcurrently r_f1_idx    on sn.r (f1 asc);
create index nonconcurrently r_f2_idx    on sn.r (f2 asc);
create index nonconcurrently r_f3_idx    on sn.r (f3 asc);
create index nonconcurrently r_f4_idx    on sn.r (f4 asc);
create index nonconcurrently r_f5_idx    on sn.r (f5 asc);
create index nonconcurrently r_f10_idx   on sn.r (f10 asc);
create index nonconcurrently r_f100_idx  on sn.r (f100 asc);
create index nonconcurrently r_f1000_idx on sn.r (f1000 asc);
create index nonconcurrently r_dv1_idx   on sn.r (dv1 asc);
create index nonconcurrently r_skw1_idx  on sn.r (skw1 asc);
create index nonconcurrently r_skw2_idx  on sn.r (skw2 asc);
create index nonconcurrently r_skw3_idx  on sn.r (skw3 asc);

-- make index ybctids and table rows uncorrelated
set seed = 0.1234;

with t as (
    select
        i, rn,
        round((sqrt(-2.0 * ln(1.0 - random())) * cos(2.0 * pi() * random())) * 2.0)::integer skw1,
        round((sqrt(-2.0 * ln(1.0 - random())) * cos(2.0 * pi() * random())) * 5.0)::integer skw2,
        round((sqrt(-2.0 * ln(1.0 - random())) * cos(2.0 * pi() * random())) * 10.0)::integer skw3
    from (
        select i, row_number() over (order by random()) rn
        from generate_series(1, 12345) i
    ) v
)
insert into sn.r
    select
        rn,
        i,
        (i - 1) / 2 + 1,
        (i - 1) / 3 + 1,
        (i - 1) / 4 + 1,
        (i - 1) / 5 + 1,
        (i - 1) / 10 + 1,
        (i - 1) / 100 + 1,
        (i - 1) / 1000 + 1,
        1,
        skw1 - min(skw1) over () + 1 skw1,
        skw2 - min(skw2) over () + 1 skw2,
        skw3 - min(skw3) over () + 1 skw3,
        rn,
        lpad(sha512(i::bpchar::bytea)::bpchar, 512, '-')
    from t;

analyze sn.r;

create table sn.rC (like sn.r including constraints including indexes including statistics)
    with (colocation = on);
insert into sn.rC select * from sn.r;
analyze sn.rC;


drop table if exists queries cascade;
create table queries (qid int, query text, primary key (qid asc));
create table explain_query_options (with_analyze bool not null);
insert into explain_query_options values (false);


-- check_roundtrip_estimates compares estimated and actual roundtrips per
-- index scan node.
--
-- The labelling of "Estimated [Table|Index] Roundtrips" (planner) and
-- "Storage [Table|Index] Read Requests" (actual) is internally inconsistent
-- across scan kinds and colocation; the colocated layer bundles index+table
-- fetches into one RPC reported as an "index" read.  We therefore check two
-- things:
--
-- 1. Total roundtrips: est_table + est_index vs act_table + act_index.
--    This works uniformly because in every case one side is zero on both
--    the estimate and the actual.
--
-- 2. Per-field layout (which actual counters are reported / non-zero):
--      PK Index Scan                    : act_table > 0,  act_index = 0
--      Index Only Scan                  : act_table = 0,  act_index > 0
--      colocated     non-PK Index Scan  : act_table = 0,  act_index > 0
--      non-colocated non-PK Index Scan  : act_table > 0,  act_index > 0
--    "= 0" matches both an absent counter (omitted from EXPLAIN when zero)
--    and an explicit zero.
drop view if exists check_roundtrip_estimates;
create view check_roundtrip_estimates as
with recursive plan_tree as (
    select
        q.qid,
        q.query,
        ''::text path,
        0 depth,
        js.explain_line::jsonb #> '{0,Plan}' node
    from
        queries q,
        explain_query_options opt,
        lateral explain_query_json(q.query, opt.with_analyze) js

    union all

    select
        pt.qid,
        pt.query,
        pt.path || child.ord::text || '.' path,
        pt.depth + 1 depth,
        child.node
    from
        plan_tree pt,
        lateral jsonb_array_elements(coalesce(pt.node->'Plans', '[]'::jsonb))
            with ordinality as child(node, ord)
),
plan_rows as (
    select
        pt.qid,
        pt.query,
        pt.path,
        pt.depth,
        rec.*
    from
        plan_tree pt,
        lateral jsonb_populate_record(
            null::index_plan_rows,
            coalesce(pt.node, '{}'::jsonb)
        ) rec
),
index_nodes as (
    select *
    from plan_rows
    where "Node Type" like 'Index% Scan%'
)
select
    qid,
    query,
    nid,
    "Node Type",
    "Index Name",
    is_colocated,
    scan_kind,
    "Index Cond",
    est_table "Estimated Table Roundtrips",
    est_index "Estimated Index Roundtrips",
    act_table "Storage Table Read Requests",
    act_index "Storage Index Read Requests",
    estimated_roundtrips "Estimated Roundtrips",
    actual_roundtrips "Actual Roundtrips",
    case when error_roundtrips > 0.05 and abs(diff_roundtrips) > 1 then
        'failed' else 'passed' end "Total Estimates",
    case
        when scan_kind = 'pk'
                and act_table > 0 and act_index = 0 then 'passed'
        when scan_kind = 'ios'
                and act_table = 0 and act_index > 0 then 'passed'
        when scan_kind = 'idx' and is_colocated
                and act_table = 0 and act_index > 0 then 'passed'
        when scan_kind = 'idx' and not is_colocated
                and act_table > 0 and act_index > 0 then 'passed'
        else 'failed'
    end "Field Layout"
from (
    select
        qid,
        query,
        row_number() over (partition by qid order by path) nid,
        "Node Type"||case when "Scan Direction" = 'Backward'
            then ' Backward' else '' end "Node Type",
        "Index Name",
        "Index Name" like 'rc_%' is_colocated,
        case
            when "Index Name" like '%_pkey' then 'pk'
            when "Node Type" like 'Index Only Scan%' then 'ios'
            else 'idx'
        end scan_kind,
        "Index Cond",
        coalesce("Estimated Table Roundtrips", 0) est_table,
        coalesce("Estimated Index Roundtrips", 0) est_index,
        coalesce("Storage Table Read Requests", 0) act_table,
        coalesce("Storage Index Read Requests", 0) act_index,
        coalesce("Estimated Table Roundtrips", 0)
            + coalesce("Estimated Index Roundtrips", 0) estimated_roundtrips,
        coalesce("Storage Table Read Requests", 0)
            + coalesce("Storage Index Read Requests", 0) actual_roundtrips,
        abs(coalesce("Estimated Table Roundtrips", 0)
              + coalesce("Estimated Index Roundtrips", 0)
            - coalesce("Storage Table Read Requests", 0)
              - coalesce("Storage Index Read Requests", 0))
            / greatest(1, coalesce("Storage Table Read Requests", 0)
                          + coalesce("Storage Index Read Requests", 0))
            error_roundtrips,
        coalesce("Estimated Table Roundtrips", 0)
            + coalesce("Estimated Index Roundtrips", 0)
            - coalesce("Storage Table Read Requests", 0)
            - coalesce("Storage Index Read Requests", 0) diff_roundtrips
    from
        index_nodes
) v;
