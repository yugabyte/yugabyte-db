-- How a range bound on one join input's key changes the cost of reading the
-- other, for the two join methods that model it: the merge join's scan clamp
-- (mergejoinscansel) and the batched nested loop's leading-mismatch skip
-- (yb_bnl_outer_skip_selectivity).
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
-- loop.  "c" and "posix" select the same rows, so any cost difference between
-- them is the estimate's alone.
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
      where o.k >= ''a'' collate "POSIX" order by o.k');

------------------------------------------------------------------------------
-- 200: the conditioning ratio.  o.g groups the key 52 rows at a time, so a
-- bound on g bounds the key.  The inner starts at 131: "ge3" leaves 26 outer
-- rows below it, "ge4" leaves none.  Both must be cheaper than no bound, and
-- ge4 cheaper than ge3.  The group is reported again after creating extended
-- statistics over (g, k); see the comparison below.
------------------------------------------------------------------------------
insert into variants values
    (200, 'conditioning ratio', 1, 'none',
     'select o.k from ext_o o join ext_i i on o.k = i.k order by o.k'),
    (200, 'conditioning ratio', 2, 'ge3',
     'select o.k from ext_o o join ext_i i on o.k = i.k
      where o.g >= 3 order by o.k'),
    (200, 'conditioning ratio', 3, 'ge4',
     'select o.k from ext_o o join ext_i i on o.k = i.k
      where o.g >= 4 order by o.k');

------------------------------------------------------------------------------
-- 300: a relabeled join key.  The two table pairs hold identical values and
-- differ only in the key's declared type, so text and varchar must cost the
-- same both with and without the bound: the expected ordering pairs them up
-- ('text-ge=vchr-ge text-none=vchr-none').  If the relabeling defeats the
-- clause pairing, the varchar bound stops working and its label separates.
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
-- BNL, inner bound (100): the skip reads the inner's boundary from its
-- full-column histogram, so an inner bound cannot move it, and the two bounds
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
------------------------------------------------------------------------------
select gid, grp, method, cost_order, work_order
from skip_report
where gid in (100, 110)
order by gid, method;

------------------------------------------------------------------------------
-- 200, the conditioning ratio, before extended statistics.  Both bounds must
-- be cheaper than none, and ge4 (no rows below the inner's first key) cheaper
-- than ge3 (26 rows below it), matching the work ordering.
------------------------------------------------------------------------------
select gid, grp, method, cost_order, work_order
from skip_report
where gid = 200
order by method;

------------------------------------------------------------------------------
-- 200 again, with extended statistics over (g, k).  The conditioned fraction
-- is a ratio whose numerator lists the range clause with the restriction and
-- whose denominator lists the restriction alone; extended statistics apply to
-- a list of two or more RestrictInfos, so they can reach the numerator while
-- the single-clause denominator gives them nothing to apply to.  A ratio whose
-- halves come from different models is not a probability, so the object must
-- leave these costs alone.
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
-- orderings.  A separated varchar label means the relabeling defeated the
-- clause pairing and the bound stopped being usable on that side.
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
