--
-- Cursors
--

-- These tests require track_utility to be enabled.
SET pg_stat_statements.track_utility = TRUE;
-- YB: track_planning defaults to TRUE in YB (FALSE in upstream); set FALSE to match upstream.
SET pg_stat_statements.track_planning = FALSE;
SELECT pg_stat_statements_reset() IS NOT NULL AS t;

-- DECLARE
-- SELECT is normalized.
DECLARE cursor_stats_1 CURSOR WITH HOLD FOR SELECT 1;
CLOSE cursor_stats_1;
DECLARE cursor_stats_1 CURSOR WITH HOLD FOR SELECT 2;
CLOSE cursor_stats_1;

SELECT calls, rows, query FROM pg_stat_statements ORDER BY query COLLATE "C";
SELECT pg_stat_statements_reset() IS NOT NULL AS t;

-- FETCH
BEGIN;
DECLARE cursor_stats_1 CURSOR WITH HOLD FOR SELECT 2;
DECLARE cursor_stats_2 CURSOR WITH HOLD FOR SELECT 3;
FETCH 1 IN cursor_stats_1;
FETCH 1 IN cursor_stats_2;
CLOSE cursor_stats_1;
CLOSE cursor_stats_2;
COMMIT;

SELECT calls, rows, query FROM pg_stat_statements ORDER BY query COLLATE "C";
SELECT pg_stat_statements_reset() IS NOT NULL AS t;

-- Normalization of FETCH statements
-- TODO(#6514): YB supports only forward FETCH; explicit directions and negative counts error.
-- Upstream tests every direction; here we keep the forward fetches + one representative (BACKWARD).
BEGIN;
DECLARE pgss_cursor CURSOR FOR SELECT FROM generate_series(1, 10);
FETCH pgss_cursor;
FETCH 1 pgss_cursor;
FETCH 2 pgss_cursor;
FETCH BACKWARD pgss_cursor;
COMMIT;
SELECT calls, query FROM pg_stat_statements ORDER BY query COLLATE "C";
