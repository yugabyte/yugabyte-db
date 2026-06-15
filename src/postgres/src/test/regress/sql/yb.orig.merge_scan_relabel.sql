--
-- See yb_merge_scan_schedule for details about the test.  Tests merge scan
-- with RelabelType nodes (binary-compatible casts like varchar <-> text).
--
-- Where RelabelType nodes come from for the relabel_tbl columns:
-- - v is varchar: equality uses the text operators, so a comparison wraps the
--   LHS in a RelabelType (varchar -> text).  An explicit v::text is the same.
-- - t is text: comparisons need no coercion, so the LHS is bare.  t::varchar
--   as an index expression is a RelabelType (text -> varchar); as a query
--   predicate it is const-folded back to bare t.
--
\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/merge_scan_setup.sql'
\i :filename

-- Compare behavior between
-- - regular index scan using columns considered redundant for being constant
-- - merge index scan using SAOP stream columns
SELECT $$= '1'$$ AS "R1" \gset
SELECT $$IN ('1', '1')$$ AS "R2" \gset
\set Qnext :iter_R2

--
-- Index 1: v and t merge streams on bare leading columns.
--
CREATE INDEX NONCONCURRENTLY idx ON relabel_tbl (v ASC, t, i2, (v::text), v, (t::varchar), t, i4)
SPLIT AT VALUES (('0'), ('1'), ('2'), ('3'));

-- RelabelType strip points exercised:
-- - SAOP clause LHS: v's LHS is wrapped, but the leading index expression is
--   bare, so the LHS must be stripped to match.  (t streams bare on both
--   sides: the no-relabel baseline.)
-- - Index expression: the redundant (v::text) and (t::varchar) must be
--   stripped on entry to match their stored SAOPs.
-- - Redundancy check's stored LHS: v's stored SAOP LHS keeps its RelabelType
--   and must be stripped to match the redundant v columns.  t's stored LHS is
--   bare.
\set query ':P :Q SELECT i2, i4, n, v, t::varchar FROM relabel_tbl WHERE v :R AND t :R ORDER BY i2, i4, n LIMIT 5;'
\i :iter_P2

-- Explicit-cast predicates behave identically: v::text parses to the same
-- wrapped LHS as bare v, and t::varchar is const-folded back to bare t.
\set query ':P :Q SELECT i2, i4, n, v::text, t::varchar FROM relabel_tbl WHERE v::text :R AND t::varchar :R ORDER BY i2, i4, n LIMIT 5;'
\i :iter_P2

DROP INDEX idx;

--
-- Index 2: v and t merge streams on cast leading columns.
--
CREATE INDEX NONCONCURRENTLY idx ON relabel_tbl ((v::text) ASC, (t::varchar), i2, (v::text), v, (t::varchar), t, i4)
SPLIT AT VALUES (('0'), ('1'), ('2'), ('3'));

-- Beyond index 1:
-- - The leading (v::text) is wrapped on both sides: the index expression and
--   the SAOP LHS must each be stripped.
-- - The (t::varchar) stream column is wrapped on the index expression side
--   only.
\set query ':P :Q SELECT i2, i4, n, v, t FROM relabel_tbl WHERE v :R AND t :R ORDER BY i2, i4, n LIMIT 5;'
\i :iter_P2

DROP INDEX idx;

--
-- Index 3: minimal repro -- one bare varchar stream column, no redundant
-- columns; only the SAOP clause LHS strip is needed.
--
-- R also varies the RHS type ({=, IN} x {untyped, ::varchar}): varchar
-- constants are relabeled and const-folded back to plain text Consts, which
-- SAOP eligibility requires, so engagement does not change.
--
CREATE INDEX NONCONCURRENTLY idx ON relabel_tbl (v ASC, i2, i4)
SPLIT AT VALUES (('0'), ('1'), ('2'), ('3'));

SELECT $$= '1'::varchar$$ AS "R3" \gset
SELECT $$IN ('1'::varchar, '1'::varchar)$$ AS "R4" \gset
\set Qnext :iter_R4

\set query ':P :Q SELECT i2, i4, n, v FROM relabel_tbl WHERE v :R ORDER BY i2, i4, n LIMIT 5;'
\i :iter_P2

DROP INDEX idx;
