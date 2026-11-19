CREATE EXTENSION pg_hint_plan;
SET pg_hint_plan.enable_hint_table TO on;

-- #23547 Ensure query against a view containing stored function doesn't crash
SELECT numeric_precision FROM information_schema.columns
    WHERE table_schema = 'pg_catalog' AND table_name = 'pg_class'
          AND column_name = 'relname';

-- #35729 Rows keyed by query id and rows keyed by normalized query text both
-- match when pg_hint_plan.yb_use_query_id_for_hinting is on (the default).
-- Within an application name the query id row wins; a row for the specific
-- application name wins over a row for ''.
SET compute_query_id TO on;
SET application_name TO 'yb_app';
CREATE TABLE yb_qid (a int PRIMARY KEY, b int);
CREATE INDEX yb_qid_b ON yb_qid (b ASC);
CREATE INDEX yb_qid_b2 ON yb_qid (b ASC);
-- Read the query id from EXPLAIN rather than hard-coding it, since it changes
-- whenever query jumbling or the table's OID changes.
CREATE FUNCTION yb_query_id(q text) RETURNS text LANGUAGE plpgsql AS $$
DECLARE
  line text;
BEGIN
  FOR line IN EXECUTE 'EXPLAIN (VERBOSE, COSTS OFF) ' || q LOOP
    IF line LIKE 'Query Identifier: %' THEN
      RETURN substr(line, length('Query Identifier: ') + 1);
    END IF;
  END LOOP;
  RETURN NULL;
END $$;
SELECT yb_query_id('SELECT b FROM yb_qid WHERE b = 1') AS qid \gset
DELETE FROM hint_plan.hints;
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
  VALUES ('EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = ?;', '',
          'SeqScan(yb_qid)');
-- Text row only.
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = 1;
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
  VALUES (:'qid', '', 'IndexScan(yb_qid yb_qid_b)');
-- Query id row beats text row.
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = 1;
-- With the GUC off, only the text row matches.
SET pg_hint_plan.yb_use_query_id_for_hinting TO off;
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = 1;
RESET pg_hint_plan.yb_use_query_id_for_hinting;
-- A text row for the specific application beats a query id row for ''.
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
  VALUES ('EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = ?;', 'yb_app',
          'IndexScan(yb_qid yb_qid_b2)');
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = 1;
-- A query id row for the specific application beats everything.
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
  VALUES (:'qid', 'yb_app', 'IndexOnlyScan(yb_qid yb_qid_b)');
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = 1;
-- In PL/pgSQL the lookup runs in the planner hook, which re-jumbles the
-- rewritten query, and for a view that gives a different id.  The lookup and
-- EXPLAIN must both use the parse-time id.
CREATE VIEW yb_qid_v AS SELECT a, b FROM yb_qid;
CREATE FUNCTION yb_explain(q text) RETURNS SETOF text LANGUAGE plpgsql AS $$
BEGIN
  RETURN QUERY EXECUTE 'EXPLAIN (COSTS OFF) ' || q;
END $$;
SELECT yb_query_id('SELECT b FROM yb_qid_v WHERE b = 1') AS vqid \gset
-- No hint row yet.
SELECT yb_explain('SELECT b FROM yb_qid_v WHERE b = 1');
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
  VALUES (:'vqid', 'yb_app', 'IndexScan(yb_qid yb_qid_b2)');
SELECT yb_explain('SELECT b FROM yb_qid_v WHERE b = 1');
-- The top-level lookup uses the parse-time id, so this match shows that
-- yb_query_id() got that id from EXPLAIN inside PL/pgSQL.
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid_v WHERE b = 1;
DELETE FROM hint_plan.hints WHERE norm_query_string = :'vqid';
DROP FUNCTION yb_explain;
DROP VIEW yb_qid_v;
-- Same precedence without the hint table cache, which can only be set at
-- connection start.  Keep pg_regress's intervalstyle in the new options.
\c -reuse-previous=on "options='-c intervalstyle=postgres_verbose -c pg_hint_plan.yb_enable_hint_table_cache=off'"
SET pg_hint_plan.enable_hint_table TO on;
SET compute_query_id TO on;
SET application_name TO 'yb_app';
SHOW pg_hint_plan.yb_enable_hint_table_cache;
-- A query id row for the specific application beats everything.
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = 1;
-- A text row for the specific application beats a query id row for ''.
DELETE FROM hint_plan.hints
  WHERE norm_query_string = :'qid' AND application_name = 'yb_app';
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = 1;
-- Query id row beats text row.
DELETE FROM hint_plan.hints WHERE application_name = 'yb_app';
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = 1;
-- Text row only.
DELETE FROM hint_plan.hints WHERE norm_query_string = :'qid';
EXPLAIN (COSTS OFF) SELECT b FROM yb_qid WHERE b = 1;
DELETE FROM hint_plan.hints;
DROP FUNCTION yb_query_id;
DROP TABLE yb_qid;
RESET application_name;
RESET compute_query_id;
