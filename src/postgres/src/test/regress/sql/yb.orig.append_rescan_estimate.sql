-- Test the planner's estimate of what one rescan of a nested loop's
-- partitioned inner side runs and returns against what EXPLAIN reports.
-- DEPENDENCY: yb.orig.bnl_cost_parity_setup
\c bnl_cost_parity_test
set client_min_messages to 'warning';

-- A nested loop rescans its inner side for each outer row.  When the inner
-- side is an Append over the partitions of a table and the join clauses
-- equate every partition key column with a value of the outer row, run-time
-- pruning leaves at most one partition for each rescan, and the Append is
-- costed as one partition (#35742).  For the Append under the nested loop of
-- each query, this test compares
--   the Append's cost, less its own cost for each row, with the cost of the
--     subplans that ran for each rescan, each at its own estimate;
--   the Append's estimated rows with its actual rows for each rescan.
-- act_partitions is the number of subplans that ran for each rescan.

create schema ar;

-- Inner tables of 4000 rows with k = 1..4000 and outer rows for 80 of the
-- keys, 20 in each quarter of the range.  The other columns follow from the
-- key:
--   g    : 8 values, 500 keys each
--   a, b : (a, b) is unique, with 40 values of a and 100 values of b
create table ar.s (id int, g int, a int, b int, primary key (id asc));
insert into ar.s
    select k, (k / 50) % 8, (k - 1) / 100 + 1, (k - 1) % 100 + 1
    from generate_series(50, 4000, 50) k;
analyze ar.s;

create table ar.r_range (k int, v int, primary key (k asc))
    partition by range (k);
select format('create table ar.r_range_%s partition of ar.r_range for values from (%s) to (%s)',
              i, (i - 1) * 1000 + 1, i * 1000 + 1)
from generate_series(1, 4) i \gexec

create table ar.r_hash (k int, v int, primary key (k asc))
    partition by hash (k);
select format('create table ar.r_hash_%s partition of ar.r_hash for values with (modulus 4, remainder %s)',
              i, i)
from generate_series(0, 3) i \gexec

create table ar.r_list (g int, k int, v int, primary key (g asc, k asc))
    partition by list (g);
select format('create table ar.r_list_%s partition of ar.r_list for values in (%s, %s)',
              i, 2 * i, 2 * i + 1)
from generate_series(0, 3) i \gexec

create table ar.r_hash2 (a int, b int, v int, primary key (a asc, b asc))
    partition by hash (a, b);
select format('create table ar.r_hash2_%s partition of ar.r_hash2 for values with (modulus 4, remainder %s)',
              i, i)
from generate_series(0, 3) i \gexec

-- The second partition is partitioned again, by the same key in ar.r_sub and
-- by a column that no join clause equates in ar.r_subv.
create table ar.r_sub (k int, v int, primary key (k asc))
    partition by range (k);
create table ar.r_sub_1 partition of ar.r_sub for values from (1) to (1001);
create table ar.r_sub_2 partition of ar.r_sub for values from (1001) to (2001)
    partition by range (k);
create table ar.r_sub_2_1 partition of ar.r_sub_2 for values from (1001) to (1501);
create table ar.r_sub_2_2 partition of ar.r_sub_2 for values from (1501) to (2001);
create table ar.r_sub_3 partition of ar.r_sub for values from (2001) to (3001);
create table ar.r_sub_4 partition of ar.r_sub for values from (3001) to (4001);

create table ar.r_subv (k int, v int, primary key (k asc, v asc))
    partition by range (k);
create table ar.r_subv_1 partition of ar.r_subv for values from (1) to (1001);
create table ar.r_subv_2 partition of ar.r_subv for values from (1001) to (2001)
    partition by hash (v);
create table ar.r_subv_2_1 partition of ar.r_subv_2 for values with (modulus 2, remainder 0);
create table ar.r_subv_2_2 partition of ar.r_subv_2 for values with (modulus 2, remainder 1);
create table ar.r_subv_3 partition of ar.r_subv for values from (2001) to (3001);
create table ar.r_subv_4 partition of ar.r_subv for values from (3001) to (4001);

insert into ar.r_range select k, k from generate_series(1, 4000) k;
insert into ar.r_hash select * from ar.r_range;
insert into ar.r_list select (k / 50) % 8, k, v from ar.r_range;
insert into ar.r_hash2 select (k - 1) / 100 + 1, (k - 1) % 100 + 1, v from ar.r_range;
insert into ar.r_sub select * from ar.r_range;
insert into ar.r_subv select * from ar.r_range;
analyze ar.r_range, ar.r_hash, ar.r_list, ar.r_hash2, ar.r_sub, ar.r_subv;

-- Partitions of unequal sizes: 3000 rows with one value of g in the first and
-- 1000 rows with ten values of g in the second.  Three of the four outer rows
-- have the first partition's value, as three quarters of the inner rows do.
create table ar.r_skew (g int, k int, primary key (g asc, k asc))
    partition by list (g);
create table ar.r_skew_1 partition of ar.r_skew for values in (0);
create table ar.r_skew_2 partition of ar.r_skew
    for values in (1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
insert into ar.r_skew select 0, k from generate_series(1, 3000) k;
insert into ar.r_skew select (k - 1) / 100 + 1, k from generate_series(1, 1000) k;
analyze ar.r_skew;

create table ar.s_skew (id int, g int, primary key (id asc));
insert into ar.s_skew values (1, 0), (2, 0), (3, 0), (4, 5);
analyze ar.s_skew;

-- Only the first partition has an index on the join key.
create table ar.r_noidx (k int, v int) partition by range (k);
create table ar.r_noidx_1 partition of ar.r_noidx for values from (1) to (2001);
create table ar.r_noidx_2 partition of ar.r_noidx for values from (2001) to (4001);
create index on ar.r_noidx_1 (k asc);
insert into ar.r_noidx select * from ar.r_range;
analyze ar.r_noidx;

create table ar.queries (qid int primary key, descr text, settings text,
                         query text);

-- Plan and run a query as a nested loop with s outer and r inner, and return
-- the nested loop's inner child.  Memoize and Materialize are off so that the
-- inner child is the rescanned Append whatever the Append costs.
create function ar.inner_node(settings text, query text) returns jsonb
language sql as
$$
    select jsonb_path_query_first(
               explain_join_json('Leading((s r)) NestLoop(s r) Set(enable_memoize off) Set(enable_material off) '
                                 || settings,
                                 query),
               'strict $.**{0 to last} ? (@."Node Type" == "Nested Loop")')
           ->'Plans'->1;
$$;

-- An estimate fails when it is off from the measured value by more than 25%
-- of it, a measured value under one counting as one.
create function ar.verdict(est float8, act float8) returns text
language sql immutable as
$$
    select case when abs(est - act) > 0.25 * greatest(1, act)
                then 'failed' else 'passed' end;
$$;

-- The Append adds half of cpu_tuple_cost for each of its rows to the cost of
-- its subplans (cost_append).  cost_ratio is the Append's cost without that
-- over the cost of the subplans that ran, and is null when none ran.
create view ar.check_append_rescan_estimates as
select
    qid,
    descr,
    node_type,
    rescans,
    act_partitions,
    round((est_cost / nullif(act_cost, 0))::numeric, 2) cost_ratio,
    est_rows, act_rows,
    ar.verdict(est_cost, act_cost) "Cost Estimates",
    ar.verdict(est_rows, act_rows) "Row Estimates"
from (
    select
        q.qid,
        q.descr,
        n->>'Node Type'                                          node_type,
        (n->>'Actual Loops')::float8                             rescans,
        round((p.loops / (n->>'Actual Loops')::float8)::numeric, 2)::float8
                                                                 act_partitions,
        (n->>'Total Cost')::float8
            - current_setting('cpu_tuple_cost')::float8 * 0.5
              * (n->>'Plan Rows')::float8                        est_cost,
        p.ran_cost / (n->>'Actual Loops')::float8                act_cost,
        (n->>'Plan Rows')::float8                                est_rows,
        (n->>'Actual Rows')::float8                              act_rows
    from
        ar.queries q,
        lateral ar.inner_node(q.settings, q.query) n,
        lateral (
            select sum((c->>'Actual Loops')::float8) loops,
                   sum((c->>'Total Cost')::float8
                       * (c->>'Actual Loops')::float8) ran_cost
            from jsonb_array_elements(n->'Plans') c
        ) p
) v;

-- QID categories:
--   1x: the join key is the partition key, one row for each key
--   2x: several rows for each key
--   3x: a partition is partitioned again
--   4x: the join clauses or the settings leave several partitions for a
--       rescan
--   5x: no partition holds the outer rows' keys
insert into ar.queries values
    (11, 'range', '',
     'select * from ar.s s join ar.r_range r on r.k = s.id'),
    (12, 'hash', '',
     'select * from ar.s s join ar.r_hash r on r.k = s.id'),
    (13, 'hash on two columns', '',
     'select * from ar.s s join ar.r_hash2 r on r.a = s.a and r.b = s.b'),
    (21, 'list, 500 rows for each key', '',
     'select * from ar.s s join ar.r_list r on r.g = s.g'),
    (22, 'list, partitions of unequal sizes', '',
     'select * from ar.s_skew s join ar.r_skew r on r.g = s.g'),
    (31, 'partitioned again by the join key', '',
     'select * from ar.s s join ar.r_sub r on r.k = s.id'),
    (32, 'partitioned again by another column', '',
     'select * from ar.s s join ar.r_subv r on r.k = s.id'),
    (41, 'hash on two columns, one of them joined', '',
     'select * from ar.s s join ar.r_hash2 r on r.a = s.a'),
    (42, 'range, enable_partition_pruning off', 'Set(enable_partition_pruning off)',
     'select * from ar.s s join ar.r_range r on r.k = s.id'),
    (43, 'range, inequality on the partition key', '',
     'select * from ar.s s join ar.r_range r on r.k > s.id'),
    (51, 'range, keys above every partition', '',
     'select * from ar.s s left join ar.r_range r on r.k = s.id + 4000');

-- Every query runs as a nested loop over an Append.  should return no rows.
select qid, descr, node_type from ar.check_append_rescan_estimates
where node_type is distinct from 'Append'
order by qid;

-- should return only the rows explained below as 'failed'.
-- 32: a key in the partition that is partitioned by v, a column no join
-- clause equates, runs both of that partition's own partitions, so 1.25
-- partitions run for each rescan here.  The Append is costed as all five
-- partitions and their five rows.
-- 42: with enable_partition_pruning off every partition runs for each rescan,
-- and the Append is costed so.  Its row estimate is the sum of the
-- partitions' estimates, each of which takes the key to be one of the
-- partition's own keys, and three of the four partitions return no row.
-- 43: a rescan runs the partitions that hold keys above the outer row's, 2.5
-- of the four here.  The Append is costed as all four partitions.  Its row
-- estimate is the sum of the partitions' estimates, none of which takes the
-- outer row's value into account.
-- 51: no partition holds a key of the outer rows, so no partition runs.  The
-- Append is costed as one partition and one row for each rescan, because the
-- estimate does not compare the outer rows' values with the partition bounds
-- (#35742).
select qid, descr, rescans, act_partitions, cost_ratio, est_rows, act_rows,
       "Cost Estimates", "Row Estimates"
from ar.check_append_rescan_estimates
order by qid;

-- With enable_seqscan off, the scan of the partition that has no index on the
-- join key is disabled, and so is the Append over it.  should return true.
select (n->>'Total Cost')::float8 >= 1e10 append_is_disabled
from ar.inner_node('Set(enable_seqscan off)',
                   'select * from ar.s s join ar.r_noidx r on r.k = s.id') n;

drop schema ar cascade;
