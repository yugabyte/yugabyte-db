-- Tests for the hint table
LOAD 'pg_hint_plan';

-- Attempting to use the hint table without the extension created
-- emits a WARNING.
SET pg_hint_plan.enable_hint_table TO on;
SELECT 1;
SET pg_hint_plan.enable_hint_table TO off;

CREATE EXTENSION pg_hint_plan;
SET pg_hint_plan.enable_hint_table TO on;
SELECT 1;
SET pg_hint_plan.enable_hint_table TO off;
DROP EXTENSION pg_hint_plan;
