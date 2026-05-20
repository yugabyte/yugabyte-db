-- Create database objects used by parallel planner roundtrip estimate tests
set client_min_messages to 'warning';

drop database if exists roundtrip_parallel_test with (force);
create database roundtrip_parallel_test with colocation = true;
alter database roundtrip_parallel_test set yb_explain_hide_non_deterministic_fields = off;
alter database roundtrip_parallel_test set yb_enable_cbo = on;

\c roundtrip_parallel_test
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

drop function if exists non_pushable cascade;
create function non_pushable(v bigint) returns bigint
language plpgsql immutable parallel safe as
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
    "Workers Launched" float8,
    "Workers Planned" float8,
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

create index nonconcurrently i_r_a on ce.r (a asc) include (v);
create unique index nonconcurrently i_r_b on ce.r (b asc) include (v);
create index nonconcurrently i_r_c on ce.r (c asc) include (v);
create index nonconcurrently i_r_bl on ce.r (bl asc) include (v);

insert into ce.r
    select
        i,
        ((i - 1) / 10) + 1,
        i,
        concat(chr((((i - 1) / 26 / 26) % 26) + ascii('a')),
               chr((((i - 1) / 26) % 26) + ascii('a')),
               chr(((i - 1) % 26) + ascii('a'))),
        i,
        ((i - 1) / 10) + 1,
        i % 2 = 1,
        sha512(('x' || i)::bytea)::bpchar
            || lpad(sha512((i || 'y')::bytea)::bpchar, 536, '#')
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

-- Force consideration of parallel paths regardless of the cost-based
-- worker count.  This sets RelOptInfo->rel_parallel_workers in plancat.c
-- and short-circuits yb_compute_parallel_worker, so the planner always
-- considers a partial path with the requested worker count.  No
-- parallel_*_cost or yb_parallel_range_* GUC is touched.
alter table ce.r set (parallel_workers = 4);
alter table ce.rC set (parallel_workers = 4);

drop schema if exists sn cascade;
create schema sn;

create table sn.r (
    pk int,
    f1 int,
    f2 int,
    f3 int,
    f4 int,
    f5 int,
    f10 int,
    f100 int,
    f1000 int,
    dv1 int,
    skw1 int,
    skw2 int,
    skw3 int,
    v int,
    vv char(512),
    primary key (pk asc)
) with (colocation = off);

alter table sn.r alter column skw1 set statistics 500;
alter table sn.r alter column skw2 set statistics 500;
alter table sn.r alter column skw3 set statistics 500;

create index nonconcurrently r_f1_idx    on sn.r (f1 asc) include (vv);
create index nonconcurrently r_f2_idx    on sn.r (f2 asc) include (vv);
create index nonconcurrently r_f3_idx    on sn.r (f3 asc) include (vv);
create index nonconcurrently r_f4_idx    on sn.r (f4 asc) include (vv);
create index nonconcurrently r_f5_idx    on sn.r (f5 asc) include (vv);
create index nonconcurrently r_f10_idx   on sn.r (f10 asc) include (vv);
create index nonconcurrently r_f100_idx  on sn.r (f100 asc) include (vv);
create index nonconcurrently r_f1000_idx on sn.r (f1000 asc) include (vv);
create index nonconcurrently r_dv1_idx   on sn.r (dv1 asc) include (vv);
create index nonconcurrently r_skw1_idx  on sn.r (skw1 asc) include (vv);
create index nonconcurrently r_skw2_idx  on sn.r (skw2 asc) include (vv);
create index nonconcurrently r_skw3_idx  on sn.r (skw3 asc) include (vv);

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

alter table sn.r set (parallel_workers = 4);
alter table sn.rC set (parallel_workers = 4);

drop table if exists queries cascade;
create table queries (qid int, query text, primary key (qid asc));
create table explain_query_options (with_analyze bool not null);
insert into explain_query_options values (false);

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
),
parallel_context as (
    select
        idx.qid,
        idx.path,
        close_parallel."Node Type" parallel_node_type,
        coalesce(close_workers."Workers Launched",
                 close_workers."Workers Planned", 0) actual_workers
    from
        index_nodes idx
        left join lateral (
            select anc."Node Type", anc.path, anc.depth
            from plan_rows anc
            where anc.qid = idx.qid
              and anc.depth < idx.depth
              and idx.path like anc.path || '%'
              and anc."Node Type" in ('Gather', 'Gather Merge', 'Parallel Append')
            order by anc.depth desc
            limit 1
        ) close_parallel on true
        left join lateral (
            select anc."Workers Launched", anc."Workers Planned"
            from plan_rows anc
            where close_parallel.path is not null
              and anc.qid = idx.qid
              and close_parallel.path like anc.path || '%'
              and anc."Node Type" in ('Gather', 'Gather Merge')
            order by anc.depth desc
            limit 1
        ) close_workers on true
)
select
    qid,
    query,
    nid,
    "Node Type",
    "Index Name",
    "Actual Workers",
    "Went Parallel",
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
        case
            when pc.actual_workers > 0 then
                'Parallel ' || "Node Type" || case when "Scan Direction" = 'Backward'
                    then ' Backward' else '' end
            else
                "Node Type" || case when "Scan Direction" = 'Backward'
                    then ' Backward' else '' end
        end "Node Type",
        "Index Name",
        pc.actual_workers "Actual Workers",
        pc.actual_workers > 0 "Went Parallel",
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
        join parallel_context pc using (qid, path)
) v;
