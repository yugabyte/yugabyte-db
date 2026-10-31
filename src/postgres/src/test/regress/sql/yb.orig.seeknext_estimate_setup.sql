-- Create database objects used by planner's seek/next estimate tests
set client_min_messages to 'warning';

drop database if exists seeknext_test with (force);
create database seeknext_test with colocation = true;
alter database seeknext_test set yb_explain_hide_non_deterministic_fields = off;
alter database seeknext_test set yb_enable_cbo = on;

\c seeknext_test
set client_min_messages to 'warning';

-- Colocation IDs are random, and a scan at either end of the colocated
-- tablet measures different steps than one between other key ranges.  These
-- non-empty tables hold the lowest and highest IDs, so no measured relation
-- sits at an end.
create table tablet_first (k int primary key) with (colocation_id = 16384);
create table tablet_last (k int primary key) with (colocation_id = 4294967295);
insert into tablet_first values (1);
insert into tablet_last values (1);

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
    "Estimated Seeks" float8,
    "Estimated Nexts" float8,
    "Estimated Prevs" float8,
    "Metric rocksdb_number_db_seek" float8,
    "Metric rocksdb_number_db_next" float8,
    "Metric rocksdb_number_db_prev" float8
);


drop table if exists r;
create table r (
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

alter table r alter column skw1 set statistics 500;
alter table r alter column skw2 set statistics 500;
alter table r alter column skw3 set statistics 500;

create index nonconcurrently r_f1_idx on r (f1 asc);
create index nonconcurrently r_f2_idx on r (f2 asc);
create index nonconcurrently r_f3_idx on r (f3 asc);
create index nonconcurrently r_f4_idx on r (f4 asc);
create index nonconcurrently r_f5_idx on r (f5 asc);
create index nonconcurrently r_f10_idx on r (f10 asc);
create index nonconcurrently r_f100_idx on r (f100 asc);
create index nonconcurrently r_f1000_idx on r (f1000 asc);
create index nonconcurrently r_dv1_idx on r (dv1 asc);
create index nonconcurrently r_skw1_idx on r (skw1 asc);
create index nonconcurrently r_skw2_idx on r (skw2 asc);
create index nonconcurrently r_skw3_idx on r (skw3 asc);

-- make index ybctids and table rows uncorrelated
set seed = 0.1234;

with t as (
    select
        i, rn,
        -- discrete normal distribution (mean = 0, sigmas = 2, 5, 50)
        round((sqrt(-2.0 * ln(1.0 - random())) * cos(2.0 * pi() * random())) * 2.0)::integer skw1,
        round((sqrt(-2.0 * ln(1.0 - random())) * cos(2.0 * pi() * random())) * 5.0)::integer skw2,
        round((sqrt(-2.0 * ln(1.0 - random())) * cos(2.0 * pi() * random())) * 10.0)::integer skw3
    from (
        select i, row_number() over (order by random()) rn
        from generate_series(1, 12345) i
    ) v
)
insert into r
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
        -- shift the values so the values starts with 1
        skw1 - min(skw1) over () + 1 skw1,
        skw2 - min(skw2) over () + 1 skw2,
        skw3 - min(skw3) over () + 1 skw3,
        rn,
        lpad(sha512(i::bpchar::bytea)::bpchar, 512, '-')
    from t;

analyze r;

create table rC (like r including constraints including indexes including statistics)
    with (colocation = on);
insert into rC select * from r;
analyze rC;

-- Composite primary key for distinct scans of the base table itself: g
-- repeats every value 10 times, so a distinct scan on it skips from group to
-- group with a seek.
create table rp (
    g int,
    pk int,
    v int,
    primary key (g asc, pk asc)
) with (colocation = off);

insert into rp select (i - 1) / 10 + 1, i, i from generate_series(1, 12345) i;

analyze rp;

create table rpC (like rp including constraints including indexes including statistics)
    with (colocation = on);
insert into rpC select * from rp;
analyze rpC;

-- Composite primary key whose leading column is unique, so a distinct scan on
-- it sees one row per group: the group is left by a skip whether or not its
-- row passes the condition on the second key column.
create table rq (
    u int,
    k int,
    v int,
    primary key (u asc, k asc)
) with (colocation = off);

insert into rq select i, i % 10 + 1, i from generate_series(1, 12345) i;

analyze rq;

create table rqC (like rq including constraints including indexes including statistics)
    with (colocation = on);
insert into rqC select * from rq;
analyze rqC;

-- Composite primary key whose second column positions the scan inside each
-- group: g repeats every value 10 times with k running 0..9 inside the group,
-- so a condition on k that first matches past the seek-forward threshold
-- costs a seek from the group's first row to the match.  The g values sit
-- 100 apart, which leaves room for IN lists whose values fall between groups.
create table ps (
    g int,
    k int,
    primary key (g asc, k asc)
) with (colocation = off);

insert into ps select (i / 10) * 100, i % 10 from generate_series(0, 9999) i;

analyze ps;

create table psC (like ps including constraints including indexes including statistics)
    with (colocation = on);
insert into psC select * from ps;
analyze psC;

-- ps's rows under a descending second key column: a forward scan meets k from
-- 9 down to 0 in each group, so an upper bound on k decides where the first
-- match sits and a lower bound matches the group's first row.
create table psd (
    g int,
    k int,
    primary key (g asc, k desc)
) with (colocation = off);

insert into psd select g, k from ps;

analyze psd;

create table psdC (like psd including constraints including indexes including statistics)
    with (colocation = on);
insert into psdC select * from psd;
analyze psdC;

-- A secondary index whose column after the prefix is nullable: each g group
-- holds three rows with a NULL k and the values k = 0..9.  The column is
-- descending, so NULLS FIRST: a forward scan meets the NULLs before any
-- value, a backward scan after every value.
create table pn (
    pk int,
    g int,
    k int,
    primary key (pk asc)
) with (colocation = off);

create index nonconcurrently pn_g_k_idx on pn (g asc, k desc);

insert into pn
    select i, (i / 13) * 100, case when i % 13 < 3 then null else i % 13 - 3 end
    from generate_series(0, 12999) i;

analyze pn;

create table pnC (like pn including constraints including indexes including statistics)
    with (colocation = on);
insert into pnC select * from pn;
analyze pnC;

-- pn's rows under an ascending, so NULLS LAST, column after the prefix: a
-- backward scan meets the NULLs before any value, a forward scan after every
-- value.
create table pna (
    pk int,
    g int,
    k int,
    primary key (pk asc)
) with (colocation = off);

create index nonconcurrently pna_g_k_idx on pna (g asc, k asc);

insert into pna select * from pn;

analyze pna;

create table pnaC (like pna including constraints including indexes including statistics)
    with (colocation = on);
insert into pnaC select * from pna;
analyze pnaC;

-- pn's rows under a partial index that leaves the NULLs out.
create table pnp (
    pk int,
    g int,
    k int,
    primary key (pk asc)
) with (colocation = off);

create index nonconcurrently pnp_g_k_idx on pnp (g asc, k asc) where k is not null;

insert into pnp select * from pn;

analyze pnp;

create table pnpC (like pnp including constraints including indexes including statistics)
    with (colocation = on);
insert into pnpC select * from pnp;
analyze pnpC;

-- An enum column after the prefix, ten labels per g group.  The opfamily
-- compares enum labels as anyenum rather than as the enum type itself.
create type pe_label as enum ('e0', 'e1', 'e2', 'e3', 'e4', 'e5', 'e6', 'e7', 'e8', 'e9');

create table pe (
    g int,
    m pe_label,
    primary key (g asc, m asc)
) with (colocation = off);

insert into pe select (i / 10) * 100, ('e' || i % 10)::pe_label from generate_series(0, 9999) i;

analyze pe;

create table peC (like pe including constraints including indexes including statistics)
    with (colocation = on);
insert into peC select * from pe;
analyze peC;

-- A two-column prefix (a, b) whose groups hold five rows each, one per value
-- of c.  The ndistinct statistics give the planner the (a, b) group count;
-- without them PostgreSQL clamps a multi-column group count to a tenth of
-- the rows, which halves it here.  c's statistics target makes ANALYZE read
-- every row: a skip inside a five-row group sits next to the seek-forward
-- threshold, where a sampling error in c's frequencies moves the estimate.
create table pt (
    a int,
    b int,
    c int,
    primary key (a asc, b asc, c asc)
) with (colocation = off);

alter table pt alter column c set statistics 200;

insert into pt select a, b, c from generate_series(1, 200) a, generate_series(1, 50) b, generate_series(1, 5) c;

create statistics pt_ab (ndistinct) on a, b from pt;

analyze pt;

create table ptC (like pt including constraints including indexes including statistics)
    with (colocation = on);
alter table ptC alter column c set statistics 200;
insert into ptC select * from pt;
analyze ptC;

-- Hash-sharded primary key: the hash code leads the prefix, so a distinct scan
-- on h skips from hash group to hash group inside each tablet.  Hash-sharded
-- tables cannot be colocated, so there is no rhC.
create table rh (
    h int,
    r int,
    v int,
    primary key ((h) hash, r asc)
) with (colocation = off) split into 3 tablets;

insert into rh select i / 10, i % 10, i from generate_series(0, 9999) i;

analyze rh;


drop table if exists queries cascade;
create table queries (qid int, query text, primary key (qid asc));
create table explain_query_options (with_analyze bool not null);
insert into explain_query_options values (false);

drop view if exists check_seeknext_estimates;
create view check_seeknext_estimates as
select
    qid,
    query,
    nid,
    "Node Type",
    "Index Name",
    "Index Cond",
    "Storage Index Filter",
    "Storage Filter",
    "Filter",
    case when error_seeks > 0.05 and abs(diff_seeks) > 1 then
        'failed' else 'passed' end "Seek Estimates",
    case when (error_nexts > 0.05 and abs(diff_nexts) > 1) then
        'failed' else 'passed' end "Next Estimates",
    case when (error_prevs > 0.05 and abs(diff_prevs) > 1) then
        'failed' else 'passed' end "Prev Estimates"
from (
    select
        qid,
        query,
        nid,
        "Node Type",
        "Index Name",
        "Index Cond",
        "Storage Index Filter",
        "Storage Filter",
        "Filter",
        "Estimated Seeks",
        "Estimated Nexts",
        "Estimated Prevs",
        actual_seeks "Actual Seeks",
        actual_nexts "Actual Nexts",
        actual_prevs "Actual Prevs",
        abs("Estimated Seeks" - actual_seeks)
            / greatest(1, actual_seeks) error_seeks,
        abs("Estimated Nexts" - actual_nexts)
            / greatest(1, actual_nexts) error_nexts,
        abs("Estimated Prevs" - actual_prevs)
            / greatest(1, actual_prevs) error_prevs,
        "Estimated Seeks" - actual_seeks diff_seeks,
        "Estimated Nexts" - actual_nexts diff_nexts,
        "Estimated Prevs" - actual_prevs diff_prevs
    from (
        select
            qid,
            query,
            row_number() over (partition by qid) nid,
            "Node Type"||case when "Scan Direction" = 'Backward'
                then ' Backward' else '' end "Node Type",
            "Index Name",
            "Index Cond",
            "Storage Index Filter",
            "Storage Filter",
            "Filter",
            "Plan Rows",
            "Total Cost",
            "Estimated Seeks",
            "Estimated Nexts",
            "Estimated Prevs",
            coalesce("Metric rocksdb_number_db_seek", 0) actual_seeks,
            coalesce("Metric rocksdb_number_db_next", 0) actual_nexts,
            coalesce("Metric rocksdb_number_db_prev", 0) actual_prevs
        from
            queries,
            explain_query_options opt,
            lateral explain_query_json(query, opt.with_analyze) js,
            lateral jsonb_path_query(
                js.explain_line::jsonb,
                'strict $.** ? (@."Node Type" like_regex "Index.* Scan")'
            ) pln,
            lateral jsonb_populate_record(
                null::index_plan_rows,
                coalesce(pln, '{}'::jsonb) || coalesce(pln->'Read Metrics', '{}'::jsonb)
            ) rec
    ) v
) vv;
