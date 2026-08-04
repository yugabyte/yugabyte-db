--
-- YB: YugabyteDB-lineage analog of upstream oldextversions.
--
-- YB analog of upstream's oldextversions. YB ships a different extension lineage, so this
-- walks YB's supported upgrade path (1.10-yb-1.0 -> 1.10-yb-2.0 -> 1.10-yb-2.1 -> 1.13-yb-1.0)
-- and checks each step's schema matches a fresh install of the final version.
-- pg_stat_statements is pre-loaded in YB, so drop it first to install the oldest version clean.
SET client_min_messages = 'warning';
DROP EXTENSION IF EXISTS pg_stat_statements;
RESET client_min_messages;

-- Oldest YB version that ships a full-install script.
CREATE EXTENSION pg_stat_statements WITH VERSION '1.10-yb-1.0';
\d pg_stat_statements
SELECT count(*) >= 0 AS callable FROM pg_stat_statements;

ALTER EXTENSION pg_stat_statements UPDATE TO '1.10-yb-2.0';
\d pg_stat_statements
SELECT count(*) >= 0 AS callable FROM pg_stat_statements;

ALTER EXTENSION pg_stat_statements UPDATE TO '1.10-yb-2.1';
\d pg_stat_statements
SELECT count(*) >= 0 AS callable FROM pg_stat_statements;

ALTER EXTENSION pg_stat_statements UPDATE TO '1.13-yb-1.0';
\d pg_stat_statements
SELECT count(*) >= 0 AS callable FROM pg_stat_statements;
-- The minmax_only argument (from upstream 1.11) is folded into the 1.13-yb-1.0 step.
SELECT pg_get_functiondef('pg_stat_statements_reset'::regproc);

-- Equivalence: the schema reached by upgrading must match a fresh 1.13-yb-1.0 install.
SELECT pg_get_functiondef('pg_stat_statements(boolean)'::regprocedure) AS d \gset upg_
SELECT pg_get_viewdef('pg_stat_statements'::regclass) AS d \gset upg_v_
DROP EXTENSION pg_stat_statements;
CREATE EXTENSION pg_stat_statements WITH VERSION '1.13-yb-1.0';
SELECT pg_get_functiondef('pg_stat_statements(boolean)'::regprocedure) AS d \gset fresh_
SELECT pg_get_viewdef('pg_stat_statements'::regclass) AS d \gset fresh_v_
SELECT :'upg_d'   = :'fresh_d'   AS function_upgraded_equals_fresh;
SELECT :'upg_v_d' = :'fresh_v_d' AS view_upgraded_equals_fresh;

-- YB: leave the extension dropped so the next yb.orig.* entry starts clean.
DROP EXTENSION pg_stat_statements;
