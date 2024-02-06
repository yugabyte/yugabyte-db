ALTER TABLE cron.job ALTER COLUMN jobname TYPE text;

DROP FUNCTION cron.unschedule(name);
CREATE FUNCTION cron.unschedule(job_name text)
    RETURNS bool
    LANGUAGE C STRICT
    AS 'MODULE_PATHNAME', $$cron_unschedule_named$$;
COMMENT ON FUNCTION cron.unschedule(text)
    IS 'unschedule a pg_cron job';
