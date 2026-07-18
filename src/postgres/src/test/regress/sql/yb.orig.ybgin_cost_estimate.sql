--
-- Yugabyte-owned test for ybgin index access method and cost estimates.
--

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename
\set P1 ':explain'
\set P2
\set explain 'EXPLAIN (costs off)'

SET enable_seqscan = off;
SHOW yb_test_ybgin_disable_cost_factor;
\set Q1
\set Q2 '/*+SET(yb_test_ybgin_disable_cost_factor 0.5)*/'

-- Given what is currently supported, these queries would fail, so they should
-- use sequential scan.
-- GIN_SEARCH_MODE_ALL
\set query ':explain :Q SELECT * FROM vectors WHERE v @@ to_tsquery(''simple'', ''!aa'');'
\i :iter_Q2
-- multiple scan keys
\set query ':explain :Q SELECT * FROM vectors WHERE v @@ ''a'' and v @@ ''bb'';'
\i :iter_Q2
-- multiple required scan entries
-- TODO(jason): this kind of query is hard to detect during cost estimation.
-- Though it can be done, it would be a lot of work, and in that case, it is
-- probably better to spend the time supporting the query instead.  So it
-- currently will suggest index scan, unfortunately.
\set query ':explain :Q SELECT * FROM vectors WHERE v @@ to_tsquery(''simple'', ''aa | bb'');'
\i :iter_Q2
-- GIN_CAT_NULL_KEY
-- TODO(jason): this kind of query is hard to detect during cost estimation.
-- Though it can be done, it would be a lot of work, and in that case, it is
-- probably better to spend the time supporting the query instead.  So it
-- currently will suggest index scan, unfortunately.
\set query ':explain :Q SELECT * FROM arrays WHERE a @> ''{null}'';'
\i :iter_Q2
CREATE INDEX NONCONCURRENTLY idx_partial ON arrays
    USING ybgin (a)
    WHERE a <@ '{1}' or a @> '{}' or a is null;
-- GIN_SEARCH_MODE_INCLUDE_EMPTY
\set query ':explain :Q SELECT * FROM arrays WHERE a <@ ''{1}'';'
\i :iter_Q2
-- GIN_SEARCH_MODE_ALL
\set query ':explain :Q SELECT * FROM arrays WHERE a @> ''{}'';'
\i :iter_Q2
-- GIN_SEARCH_MODE_EVERYTHING, GIN_CAT_NULL_ITEM
\set query ':explain :Q SELECT * FROM arrays WHERE a is null;'
\i :iter_Q2
-- Cleanup
DROP INDEX idx_partial;
-- multiple required scan entries
-- TODO(jason): this kind of query is hard to detect during cost estimation.
-- Though it can be done, it would be a lot of work, and in that case, it is
-- probably better to spend the time supporting the query instead.  So it
-- currently will suggest index scan, unfortunately.
\set query ':explain :Q SELECT * FROM jsonbs WHERE j ?| ''{"ggg", "eee"}'';'
\i :iter_Q2
-- GIN_SEARCH_MODE_ALL
\set query ':explain :Q SELECT * FROM jsonbs WHERE j @? ''$.aaa[*] ? (@ > 2)'';'
\i :iter_Q2

-- On the other hand, these queries would succeed, so they should use index
-- scan.
\set query ':explain :Q SELECT * FROM vectors WHERE v @@ to_tsquery(''simple'', ''(aa | bb) & (cc & dd)'');'
\i :iter_Q2
