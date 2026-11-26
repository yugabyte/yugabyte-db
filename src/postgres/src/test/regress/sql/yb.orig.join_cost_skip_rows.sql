-- How a range bound on a join input's key changes the cost of reading that
-- input or the other one, for the two join methods that model it: the merge
-- join's scan fractions (mergejoinscansel) and the batched nested loop's
-- leading-mismatch skip (yb_bnl_outer_skip_selectivity).
--
-- DEPENDENCY: yb.orig.join_cost_skip_rows_setup
--
-- Each group runs several predicate VARIANTS of one query under one join
-- method.  The variants differ only in their bound, so they plan to the same
-- shape (asserted up front) and their costs are directly comparable.  The
-- report collapses a group to one row per (group, method) holding two
-- orderings of its variants:
--   cost_order  variants ordered by what the planner charges
--               (startup cost, then total cost), cheapest first.
--   work_order  variants ordered by the deterministic work the plan did
--               (rows scanned, then read requests), least first.
-- Variants tied on an ordering's keys are joined by '=' (e.g.
-- 'gt10=ge11 ge10 none'), so an equality claim is as readable as an ordering
-- one, and the keys compare exactly, so any real movement breaks a tie
-- visibly.
--
-- No cost value is printed.  The claim each group makes is a relationship
-- between its variants, and printing the numbers invites the opposite of
-- review: a cost that moves for an unrelated reason looks like an expected
-- update, gets pasted in, and the relationship the test existed to pin is
-- gone without anyone reading the comment.  An ordering breaks loudly instead,
-- and does not churn when costs move together.
--
-- cost_order <> work_order means the planner ranks the bounds differently
-- from the work they save.  That is not always a defect: where the planner
-- cannot prove a bound orders the way the join does, falling back to the
-- unclamped estimate is correct and conservative.  Each group says which it
-- expects.
\c join_cost_skip_rows_test
set client_min_messages to 'warning';

delete from variants;

------------------------------------------------------------------------------
-- 100: the bound's collation.  The inner bound (100) and the outer bound
-- (110) are the two directions; only the outer one reaches the batched nested
-- loop here, since an ascending inner's upper bound does not move where its
-- keys start.  120 and 130 read the inner bound descending, where the bounds
-- swap roles: 120's lower bound stops the merge and 130's upper bound moves
-- its start.  Their bounds sit where both collations estimate the same inner
-- rows (12 and 15), inside one 16-row page, so the inner scans cost the same
-- and the merge rows differ by the clamp alone.  "c" and "posix" select the
-- same rows, so any cost difference between them is the estimate's alone.
------------------------------------------------------------------------------
insert into variants values
    (100, 'collation, inner bound', 1, 'none',
     'select o.k from cln_o o join cln_i i on o.k = i.k order by o.k'),
    (100, 'collation, inner bound', 2, 'c',
     'select o.k from cln_o o join cln_i i on o.k = i.k
      where i.k < ''q'' collate "C" order by o.k'),
    (100, 'collation, inner bound', 3, 'posix',
     'select o.k from cln_o o join cln_i i on o.k = i.k
      where i.k < ''q'' collate "POSIX" order by o.k'),

    (110, 'collation, outer bound', 1, 'none',
     'select o.k from cln_o o join cln_i i on o.k = i.k order by o.k'),
    (110, 'collation, outer bound', 2, 'c',
     'select o.k from cln_o o join cln_i i on o.k = i.k
      where o.k >= ''a'' collate "C" order by o.k'),
    (110, 'collation, outer bound', 3, 'posix',
     'select o.k from cln_o o join cln_i i on o.k = i.k
      where o.k >= ''a'' collate "POSIX" order by o.k'),

    (120, 'collation, inner bound, desc end', 1, 'none',
     'select o.k from cln_o o join cln_i i on o.k = i.k order by o.k desc'),
    (120, 'collation, inner bound, desc end', 2, 'c',
     'select o.k from cln_o o join cln_i i on o.k = i.k
      where i.k > ''n'' collate "C" order by o.k desc'),
    (120, 'collation, inner bound, desc end', 3, 'posix',
     'select o.k from cln_o o join cln_i i on o.k = i.k
      where i.k > ''n'' collate "POSIX" order by o.k desc'),

    (130, 'collation, inner bound, desc start', 1, 'none',
     'select o.k from cln_o o join cln_i i on o.k = i.k order by o.k desc'),
    (130, 'collation, inner bound, desc start', 2, 'c',
     'select o.k from cln_o o join cln_i i on o.k = i.k
      where i.k < ''p'' collate "C" order by o.k desc'),
    (130, 'collation, inner bound, desc start', 3, 'posix',
     'select o.k from cln_o o join cln_i i on o.k = i.k
      where i.k < ''p'' collate "POSIX" order by o.k desc');

------------------------------------------------------------------------------
-- 200: a restriction on a column correlated with the key.  o.g groups the
-- key 52 rows at a time, so a bound on g bounds the key, but neither join
-- reads it as one: both condition only on bounds on the join key itself, so
-- the bounds act through the outer's row count.  The inner starts at 131:
-- "ge3" leaves 26 outer rows below it, "ge4" leaves none.  Both must be
-- cheaper than no bound, and ge4 cheaper than ge3.  The group is reported
-- again after creating extended statistics over (g, k); see the comparison
-- below.
------------------------------------------------------------------------------
insert into variants values
    (200, 'correlated restriction', 1, 'none',
     'select o.k from ext_o o join ext_i i on o.k = i.k order by o.k'),
    (200, 'correlated restriction', 2, 'ge3',
     'select o.k from ext_o o join ext_i i on o.k = i.k
      where o.g >= 3 order by o.k'),
    (200, 'correlated restriction', 3, 'ge4',
     'select o.k from ext_o o join ext_i i on o.k = i.k
      where o.g >= 4 order by o.k');

------------------------------------------------------------------------------
-- 300: a relabeled join key.  The two table pairs hold identical values and
-- differ only in the key's declared type, so text and varchar must cost the
-- same both with and without the bound: the expected ordering pairs them up
-- ('text-ge=vchr-ge text-none=vchr-none').  If the relabeling defeats the
-- bound matching, the varchar bound stops working and its label separates.
------------------------------------------------------------------------------
insert into variants values
    (300, 'relabeled key', 1, 'text-none',
     'select o.k from rlbt_o o join rlbt_i i on o.k = i.k order by o.k'),
    (300, 'relabeled key', 2, 'text-ge',
     'select o.k from rlbt_o o join rlbt_i i on o.k = i.k
      where o.k >= ''0251'' order by o.k'),
    (300, 'relabeled key', 3, 'vchr-none',
     'select o.k from rlbv_o o join rlbv_i i on o.k = i.k order by o.k'),
    (300, 'relabeled key', 4, 'vchr-ge',
     'select o.k from rlbv_o o join rlbv_i i on o.k = i.k
      where o.k >= ''0251'' order by o.k');

------------------------------------------------------------------------------
-- 400: where the bound falls relative to the boundary group.  o.k repeats
-- each value 26 times and the inner starts at 11.  "ge11" and "gt10" select
-- the same rows on an integer key and must tie; "ge10" keeps the 26-row group
-- below the inner's first key, so it must cost and scan more than those two
-- and less than no bound at all.
------------------------------------------------------------------------------
insert into variants values
    (400, 'boundary group', 1, 'none',
     'select o.k from bnd_o o join bnd_i i on o.k = i.k order by o.k'),
    (400, 'boundary group', 2, 'ge11',
     'select o.k from bnd_o o join bnd_i i on o.k = i.k
      where o.k >= 11 order by o.k'),
    (400, 'boundary group', 3, 'gt10',
     'select o.k from bnd_o o join bnd_i i on o.k = i.k
      where o.k > 10 order by o.k'),
    (400, 'boundary group', 4, 'ge10',
     'select o.k from bnd_o o join bnd_i i on o.k = i.k
      where o.k >= 10 order by o.k');

------------------------------------------------------------------------------
-- 500: a range restriction on the scanned input's own join key.  o.k runs
-- 1..1040 and the inner's 1..260.  "own" cuts the outer at the inner's last
-- key, so the merge reads its 260 restricted rows in full; "other" bounds the
-- inner at 200 instead, so the merge stops after 201 outer rows.  "disjoint"
-- restricts the outer above the inner's last key, so the merge stops at its
-- first row.
--
-- 510 is 500 read descending, against own_ih (781..1040): "own" cuts the
-- outer at the inner's lowest key and "disjoint" restricts it below that
-- key.  Under the batched nested loop 500's "other" needs no Sort above the
-- join here while the outer-bounded variants do, so its shape differs and
-- the group leaves it out.
--
-- 520 reads own_o against own_ih ascending, with LIMIT 1.  "disjoint" ends
-- below the inner's first key, so the merge reads all 541 rows and returns
-- none; "own" starts at that key and returns its first row at once.
--
-- 530: NULL keys, read descending, where NULLs sort first.  nul_o and nnl_o
-- differ only in nul_o's NULLs, which the restriction excludes, so the two
-- scans read the same rows.
------------------------------------------------------------------------------
insert into variants values
    (500, 'own restriction', 1, 'own',
     'select o.k from own_o o join own_i i on o.k = i.k
      where o.k <= 260 order by o.k'),
    (500, 'own restriction', 2, 'other',
     'select o.k from own_o o join own_i i on o.k = i.k
      where i.k <= 200 order by o.k'),
    (500, 'own restriction', 3, 'disjoint',
     'select o.k from own_o o join own_i i on o.k = i.k
      where o.k >= 500 order by o.k'),

    (510, 'own restriction, desc', 1, 'own',
     'select o.k from own_o o join own_ih i on o.k = i.k
      where o.k >= 781 order by o.k desc'),
    (510, 'own restriction, desc', 2, 'disjoint',
     'select o.k from own_o o join own_ih i on o.k = i.k
      where o.k <= 541 order by o.k desc'),

    (520, 'own restriction, limit 1', 1, 'own',
     'select o.k from own_o o join own_ih i on o.k = i.k
      where o.k >= 781 order by o.k limit 1'),
    (520, 'own restriction, limit 1', 2, 'disjoint',
     'select o.k from own_o o join own_ih i on o.k = i.k
      where o.k <= 541 order by o.k limit 1'),

    (530, 'null keys, desc', 1, 'nullable',
     'select o.k from nul_o o join own_i i on o.k = i.k
      where o.k <= 100 order by o.k desc'),
    (530, 'null keys, desc', 2, 'nonnull',
     'select o.k from nnl_o o join own_i i on o.k = i.k
      where o.k <= 100 order by o.k desc');

select gid, vid, label, query from variants order by gid, vid;

------------------------------------------------------------------------------
-- Plan validation.  Every (variant, method) pair must have realized the
-- intended join method with o outer and i inner.  Expect no rows: if any
-- appear the hints failed and the orderings below are meaningless.
-- skip_plans only plans the queries, so nothing is executed here.
------------------------------------------------------------------------------
select gid, vid, label, method, join_node_type, outer_alias, inner_alias
from skip_plans
where not (method_ok and leading_ok)
order by gid, vid, method;

------------------------------------------------------------------------------
-- Shape validation.  A group's variants are only comparable if they plan to
-- the same shape under a given method.  Expect no rows.
------------------------------------------------------------------------------
select gid, method, count(distinct shape) shapes
from skip_plans
group by gid, method
having count(distinct shape) > 1
order by gid, method;

------------------------------------------------------------------------------
-- Run every (variant, method) pair once and keep the result; every report
-- below reads this snapshot.
------------------------------------------------------------------------------
delete from costs;
insert into costs select * from skip_costs;

------------------------------------------------------------------------------
-- Row-count validation.  A variant returns the same rows whichever method
-- computes it.  Expect no rows.
------------------------------------------------------------------------------
select gid, vid, label, min(actual_rows) lo, max(actual_rows) hi
from costs
group by gid, vid, label
having min(actual_rows) <> max(actual_rows)
order by gid, vid;

------------------------------------------------------------------------------
-- 100/110, collation.
--
-- MJ, inner bound (100): the bound lowers where the merge stops reading the
-- outer, but only when it compares under the merge ordering's own collation.
-- "c" does; "posix" is an equivalent collation the planner cannot equate, so
-- it falls back to the unclamped outer and is costed above "c" while doing
-- exactly the same work -- cost_order and work_order disagree, and that
-- conservative fallback is the expected result, not a defect.
--
-- BNL, inner bound (100): the skip depends only on where the inner's keys
-- start, which an upper bound on the inner does not move, so the two bounds
-- are costed identically.  The work splits them anyway: "c" matches the
-- index's collation and becomes an index condition that ends the probe early,
-- while "posix" is left as a filter and reads what no bound at all reads.
-- That is the inner scan's own predicate handling rather than the join clamp,
-- and it runs the other way from the merge row above -- here the planner
-- credits a bound with a saving it does not get.
--
-- Outer bound (110): the bound decides how many leading outer rows sort below
-- the inner's first key.  Both methods charge those rows to startup, and both
-- orderings agree: "posix" lands between "c" and no bound, costed without the
-- clamp and scanning accordingly.
--
-- MJ, inner bound read descending, lower bound (120): the merge stops at the
-- inner's lower bound instead of its upper one, and the clamp follows the
-- direction, so the row reads as 100's does; a "c=posix" cost tie would mean
-- the end clamp went back to ascending orders only (#34905).
--
-- MJ, inner bound read descending, upper bound (130): the bound moves the
-- merge's start instead.  The 11 outer rows above it are read and discarded
-- before the first match by "c" and "posix" alike; the clamp charges them to
-- startup for "c", while "posix" falls back to the unclamped start as at 100
-- and carries less startup cost for the same work.  That disagreement is the
-- expected fallback; a "c=posix" cost tie would mean the start clamp went
-- back to ascending orders only (#34905).
--
-- BNL, inner bound read descending (120, 130): the two bounds are costed
-- identically and "c" scans the fewest rows, as at 100.  At 120 the lower
-- bound is on the side where a descending inner's keys end, which does not
-- move the skip.  At 130 the upper bound moves where they start, so "c" is
-- charged a skip of the 11 outer rows from 'p' up ("posix" fails the
-- collation check), but a skip within the first batch leaves the startup
-- cost unchanged.  Unlike 100, "posix" also does less work than no
-- bound: its filter still scans all 26 inner rows but leaves 12 or 15, which
-- fit in one 16-row page, where 100's leaves 16, which fill the page, so a
-- second request follows.  The planner's credit to "posix" over no bound,
-- unearned at 100, is earned here.
------------------------------------------------------------------------------
select gid, grp, method, cost_order, work_order
from skip_report
where gid in (100, 110, 120, 130)
order by gid, method;

------------------------------------------------------------------------------
-- 200, the correlated restriction, before extended statistics.  Both bounds
-- must be cheaper than none, and ge4 (no rows below the inner's first key)
-- cheaper than ge3 (26 rows below it), matching the work ordering.
------------------------------------------------------------------------------
select gid, grp, method, cost_order, work_order
from skip_report
where gid = 200
order by method;

------------------------------------------------------------------------------
-- 200 again, with extended statistics over (g, k).  The estimates use only
-- per-column selectivities of the join key, and g never enters them, so the
-- object must leave these costs alone.
--
-- The check compares the raw costs of every (variant, method) pair before and
-- after, symmetrically.  It asserts equality without printing a cost, and it
-- does not churn: costs that move together still cancel.  Expect no rows.
------------------------------------------------------------------------------
create temp table ext_before as
select gid, vid, label, method, startup_cost, total_cost, rows_scanned
from costs where gid = 200;

create statistics ext_o_g_k on g, k from ext_o;
analyze ext_o;

-- Re-run group 200 only, replacing its rows in the snapshot.
delete from costs where gid = 200;
insert into costs select * from skip_costs where gid = 200;

(table ext_before
 except all
 select gid, vid, label, method, startup_cost, total_cost, rows_scanned
 from costs where gid = 200)
union all
(select gid, vid, label, method, startup_cost, total_cost, rows_scanned
 from costs where gid = 200
 except all
 table ext_before);

-- The orderings must also still hold, for the same reason.
select gid, grp, method, cost_order, work_order
from skip_report
where gid = 200
order by method;

drop statistics ext_o_g_k;
analyze ext_o;
drop table ext_before;
-- Restore the no-statistics snapshot for group 200.
delete from costs where gid = 200;
insert into costs select * from skip_costs where gid = 200;

------------------------------------------------------------------------------
-- 300, a relabeled join key.  text and varchar must pair up in both
-- orderings.  A separated varchar label means the relabeling hid the varchar
-- bound from the bound matching and the bound stopped being usable.
------------------------------------------------------------------------------
select gid, grp, method, cost_order, work_order
from skip_report
where gid = 300
order by method;

------------------------------------------------------------------------------
-- 400, the boundary group.  "ge11" and "gt10" must tie, "ge10" must sit
-- between them and "none", and the work ordering must agree throughout: this
-- is the group where the planner has everything it needs, so a disagreement
-- here is a defect.
------------------------------------------------------------------------------
select gid, grp, method, cost_order, work_order
from skip_report
where gid = 400
order by method;

------------------------------------------------------------------------------
-- 500-520, the own restriction, ranked by total cost.  The variants restrict
-- different inputs, and an outer scan's startup cost moves with its row
-- estimate, so the startup-first cost_order would rank them by that alone.
-- What the restriction changes is how much of the outer the merge reads,
-- which only the total carries; under 520's LIMIT the total is the Limit's,
-- which the merge's startup cost dominates.
--
-- MJ: the full-column fraction puts the merge's end at P(o.k <= 260), a
-- quarter of the outer, so "own" would be charged for about 65 of the 260
-- rows it reads and come out cheaper than "other", which reads fewer.
-- Conditioned on the restriction the end fraction is 1, and the two
-- orderings agree (#34913).  Conditioned the same way, "disjoint"'s end
-- fraction is 0, which mergejoinscansel() would disbelieve against its
-- start of 0, charging a full scan of the 541 rows; kept at one row, it is
-- costed cheapest, as it works.  510 orders the same way, through the
-- descending comparisons > and >=.
--
-- MJ, 520: the "disjoint" outer conditions to a start fraction of 1, which
-- would be disbelieved the same way, leaving the merge no startup cost and
-- making it the cheaper variant under LIMIT 1.  Kept one row short of all,
-- the start charges the rows read before any match, and the merge is costed
-- above "own" as it works.
--
-- BNL: the batched nested loop probes every outer row, so "own" reads a
-- quarter of the outer "other" reads and is costed accordingly.  "disjoint"
-- probes all 541 outer rows and finds nothing, and lands between the two in
-- both orderings; in 510 and 520 it is costed and works above "own".
------------------------------------------------------------------------------
with m as (
    select gid, grp, label, method,
           rank() over (partition by gid, method
                        order by total_cost, startup_cost) cost_rank,
           rank() over (partition by gid, method
                        order by rows_scanned, read_reqs)  work_rank
    from costs
    where gid in (500, 510, 520)
)
select
    gid,
    grp,
    method,
    rank_labels(array_agg(label order by cost_rank, label),
                array_agg(cost_rank order by cost_rank, label)) total_order,
    rank_labels(array_agg(label order by work_rank, label),
                array_agg(work_rank order by work_rank, label)) work_order
from m
group by gid, grp, method
order by gid, method;

------------------------------------------------------------------------------
-- 530, NULL keys.  Read descending, NULLs sort first, and mergejoinscansel()
-- adds the key's NULL fraction to both fractions for the rows the merge skips
-- before the first key.  The restriction already excludes those NULLs, and a
-- fraction conditioned on it is a share of the rows left, so neither variant
-- has any to skip and both orderings tie (#34913).  The batched nested loop
-- has no NULL term and ties too.
------------------------------------------------------------------------------
select gid, grp, method, cost_order, work_order
from skip_report
where gid = 530
order by method;

------------------------------------------------------------------------------
-- A Param bound, in a generic plan.  It carries no value to condition on, so
-- the merge keeps the full-column fraction, and a lower and an upper Param
-- bound cost the same up to their scans' own difference in the second
-- decimal.  Conditioned through clauselist_selectivity() instead, the lower
-- bound would pair with the end fraction's upper comparison, the pair would
-- be replaced by DEFAULT_RANGE_INEQ_SEL, and the two costs would split
-- (#34913).  The hints sit in PREPARE, where pg_hint_plan reads them for a
-- prepared statement.
------------------------------------------------------------------------------
prepare own_ge(int) as /*+ Leading((o i)) MergeJoin(o i) */
    select o.k from own_o o join own_i i on o.k = i.k
    where o.k >= $1 order by o.k;
prepare own_le(int) as /*+ Leading((o i)) MergeJoin(o i) */
    select o.k from own_o o join own_i i on o.k = i.k
    where o.k <= $1 order by o.k;
set plan_cache_mode = force_generic_plan;

create temp table param_plans as
select label, explain_join_json('', 'execute ' || stmt, false) plan
from (values ('ge', 'own_ge(200)'), ('le', 'own_le(200)')) v(label, stmt);

-- Both are generic merge joins with o outer.
select label,
       plan->0->'Plan'->>'Node Type'                               join_node,
       side_alias(plan->0->'Plan'->'Plans'->0)                     outer_alias,
       jsonb_path_exists(plan, 'strict $.**."Index Cond" ? (@ like_regex "\\$1")')
                                                                    generic
from param_plans
order by label;

-- The two merge costs must match.
select abs(max((plan->0->'Plan'->>'Total Cost')::float8)
           - min((plan->0->'Plan'->>'Total Cost')::float8)) < 1.0 same_cost
from param_plans;

drop table param_plans;
reset plan_cache_mode;
deallocate own_ge;
deallocate own_le;
