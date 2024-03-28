--
-- Yugabyte-owned test for ybgin index access method and cost estimates.
--

SET enable_seqscan = off;
SHOW yb_test_ybgin_disable_cost_factor;
\set run 'EXPLAIN (costs off) :query; /*+SET(yb_test_ybgin_disable_cost_factor 0.5)*/ EXPLAIN (costs off) :query'

-- Given what is currently supported, these queries would fail, so they should
-- use sequential scan.
-- GIN_SEARCH_MODE_ALL
\set query 'SELECT * FROM vectors WHERE v @@ to_tsquery(''simple'', ''!aa'')'
:run;
-- multiple scan keys
\set query 'SELECT * FROM vectors WHERE v @@ ''a'' and v @@ ''bb'''
:run;
-- multiple required scan entries
-- TODO(jason): this kind of query is hard to detect during cost estimation.
-- Though it can be done, it would be a lot of work, and in that case, it is
-- probably better to spend the time supporting the query instead.  So it
-- currently will suggest index scan, unfortunately.
\set query 'SELECT * FROM vectors WHERE v @@ to_tsquery(''simple'', ''aa | bb'')'
:run;
-- GIN_CAT_NULL_KEY
-- TODO(jason): this kind of query is hard to detect during cost estimation.
-- Though it can be done, it would be a lot of work, and in that case, it is
-- probably better to spend the time supporting the query instead.  So it
-- currently will suggest index scan, unfortunately.
\set query 'SELECT * FROM arrays WHERE a @> ''{null}'''
:run;
CREATE INDEX NONCONCURRENTLY idx_partial ON arrays
    USING ybgin (a)
    WHERE a <@ '{1}' or a @> '{}' or a is null;
-- GIN_SEARCH_MODE_INCLUDE_EMPTY
\set query 'SELECT * FROM arrays WHERE a <@ ''{1}'''
:run;
-- GIN_SEARCH_MODE_ALL
\set query 'SELECT * FROM arrays WHERE a @> ''{}'''
:run;
-- GIN_SEARCH_MODE_EVERYTHING, GIN_CAT_NULL_ITEM
\set query 'SELECT * FROM arrays WHERE a is null'
:run;
-- Cleanup
DROP INDEX idx_partial;
-- multiple required scan entries
-- TODO(jason): this kind of query is hard to detect during cost estimation.
-- Though it can be done, it would be a lot of work, and in that case, it is
-- probably better to spend the time supporting the query instead.  So it
-- currently will suggest index scan, unfortunately.
\set query 'SELECT * FROM jsonbs WHERE j ?| ''{"ggg", "eee"}'''
:run;
-- GIN_SEARCH_MODE_ALL
\set query 'SELECT * FROM jsonbs WHERE j @? ''$.aaa[*] ? (@ > 2)'''
:run;

-- On the other hand, these queries would succeed, so they should use index
-- scan.
\set query 'SELECT * FROM vectors WHERE v @@ to_tsquery(''simple'', ''(aa | bb) & (cc & dd)'')'
:run;
