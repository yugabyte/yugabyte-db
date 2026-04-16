/* pg_hint_plan/pg_hint_plan--1.5.1--1.5.1-yb-1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "ALTER EXTENSION pg_dbms_stats UPDATE TO '1.5.1-yb-1.0'" to load this file. \quit

CREATE FUNCTION hint_plan.yb_cache_invalidate()
    RETURNS trigger
    LANGUAGE C
    AS 'MODULE_PATHNAME', 'yb_hint_plan_cache_invalidate';
COMMENT ON FUNCTION hint_plan.yb_cache_invalidate()
    IS 'invalidate hint plan cache';

CREATE TRIGGER yb_invalidate_hint_plan_cache
AFTER INSERT OR UPDATE OR DELETE OR TRUNCATE ON hint_plan.hints
FOR EACH STATEMENT EXECUTE FUNCTION hint_plan.yb_cache_invalidate();

SELECT pg_catalog.pg_extension_config_dump('hint_plan.hints','');
SELECT pg_catalog.pg_extension_config_dump('hint_plan.hints_id_seq','');

GRANT SELECT ON hint_plan.hints TO PUBLIC;
GRANT USAGE ON SCHEMA hint_plan TO PUBLIC;
