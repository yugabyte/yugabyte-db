--
-- See yb_saop_merge_schedule for details about the test.  Do not sort on the
-- r5n.n column because that ruins the plans.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun_saop_merge.sql'
\i :filename

-- Basic join without order
-- Third hint is to show hash join rightfully does not use SAOP merge.
-- Fourth hint is to show SAOP merge in a merge join.
-- Fifth hint is to show SAOP merge in the inner side of a merge join.
\set query 'SELECT DISTINCT ON (a.r3, a.r4) * FROM r5n a JOIN r5n b ON a.r3 = b.r1 WHERE a.r1 = 9 AND a.r2 IN (3, 4, 5) LIMIT 5'
\set hint3 '/*+HashJoin(a b) Set(yb_max_saop_merge_streams 64)*/'
\set hint4 '/*+MergeJoin(a b) Set(yb_max_saop_merge_streams 64)*/'
\set hint5 '/*+MergeJoin(a b) Leading((b a)) Set(yb_max_saop_merge_streams 64)*/'
:explain5

-- Basic join with order
-- TODO(pg16): PG commit b592422095655a64d638f541df784b19b8ecf8ad enables
-- incremental sort over MergeJoin.
\set query 'SELECT DISTINCT ON (a.r3, a.r4) * FROM r5n a JOIN r5n b ON a.r3 = b.r1 WHERE a.r1 = 9 AND a.r2 IN (3, 4, 5) ORDER BY a.r3, a.r4, a.n, b.n LIMIT 5'
:explain5run5

-- Non-const equivalence prefix
-- SAOP merge should not be used.
\set query 'SELECT DISTINCT ON (r5n.r3, r5n.r4) r5n.r3, r5n.r4, r5n.r5, r5n.r2, r5n.r1, h3r2n.r1 FROM r5n JOIN h3r2n ON r5n.r1 = h3r2n.r1 WHERE r5n.r2 IN (3, 4, 5) ORDER BY r5n.r3, r5n.r4, r5n.r5 LIMIT 5'
:explain2

-- Non-const equivalence suffix
-- SAOP merge should not be used.
\set query 'SELECT * FROM r5n JOIN h3r2n ON r5n.r4 = h3r2n.r1 AND r5n.r5 = h3r2n.r2 WHERE r5n.r1 = 1 AND r5n.r2 IN (6, 7, 8, 9, 10) AND r5n.r3 IN (1, 2, 3, 4, 5) LIMIT 5'
:explain2

-- 2-hop =-const equivalence
-- Third hint is to show that when a JOIN doesn't care about the ordering of
-- the index scan, SAOP merge is not used.
-- Fourth hint is to show SAOP merge in a merge join.
-- Fifth hint is to show SAOP merge in the inner side of a merge join.
-- TODO(#29030): fifth hint should use SAOP merge.
\set query 'SELECT DISTINCT ON (r5n.r3, r5n.r4) r5n.r1, h3r2n.r1, r5n.r3, r5n.r4, r5n.r5, r5n.r2 FROM r5n JOIN h3r2n ON r5n.r1 = h3r2n.r1 AND r5n.r3 = h3r2n.h3 WHERE h3r2n.r1 = 9 AND r5n.r2 IN (3, 4, 5) LIMIT 5'
\set hint3 '/*+Leading((h3r2n r5n)) Set(yb_max_saop_merge_streams 64)*/'
\set hint4 '/*+MergeJoin(h3r2n r5n) Set(yb_max_saop_merge_streams 64)*/'
\set hint5 '/*+MergeJoin(h3r2n r5n) Leading((h3r2n r5n)) Set(yb_max_saop_merge_streams 64)*/'
:explain5run5

-- Same as above with no DISTINCT
-- SAOP merge should not be used.
\set query 'SELECT r5n.r1, h3r2n.r1, r5n.r3, r5n.r4, r5n.r5, r5n.r2 FROM r5n JOIN h3r2n ON r5n.r1 = h3r2n.r1 AND r5n.r3 = h3r2n.h3 WHERE h3r2n.r1 = 9 AND r5n.r2 IN (3, 4, 5) LIMIT 5'
:explain2

-- Multi-hop =-const equivalence
-- Third hint is to show that when a JOIN doesn't care about the ordering of
-- the index scan, SAOP merge is not used.
\set query 'SELECT DISTINCT ON (r5n.r3, r5n.r4) r5n.r1, h3r2n.r1, parent.p1, r5n.r3, r5n.r4, r5n.r5, r5n.r2 FROM r5n JOIN h3r2n ON r5n.r1 = h3r2n.r1 JOIN parent ON h3r2n.h1 = parent.p1 WHERE parent.p1 = 6 AND h3r2n.h1 = h3r2n.r1 AND r5n.r2 IN (3, 4, 5) ORDER BY r5n.r3, r5n.r4, r5n.r5 LIMIT 5'
\set hint3 '/*+Leading(((h3r2n r5n) parent)) Set(yb_max_saop_merge_streams 64)*/'
:explain3run3

-- 2-hop IN-const equivalence
-- TODO(#29030): this should use SAOP merge; may want to use :run3.
\set query 'SELECT DISTINCT ON (r5n.r3, r5n.r4) r5n.r1, h3r2n.r1, r5n.r3, r5n.r4, r5n.r5, r5n.r2 FROM r5n JOIN h3r2n ON r5n.r1 = h3r2n.r1 WHERE h3r2n.r1 IN (1, 2) AND r5n.r2 IN (3, 4, 5) ORDER BY r5n.r3, r5n.r4, r5n.r5 LIMIT 5'
:explain2run2
