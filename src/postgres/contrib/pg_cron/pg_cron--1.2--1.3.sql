/* pg_cron--1.2--1.3.sql */

CREATE SEQUENCE cron.runid_seq;
CREATE TABLE cron.job_run_details (
	jobid bigint,
	runid bigint primary key default pg_catalog.nextval('cron.runid_seq'),
	job_pid integer,
	database text,
	username text,
	command text,
	status text,
	return_message text,
	start_time timestamptz,
	end_time timestamptz
);

GRANT SELECT ON cron.job_run_details TO public;
GRANT DELETE ON cron.job_run_details TO public;
ALTER TABLE cron.job_run_details ENABLE ROW LEVEL SECURITY;
CREATE POLICY cron_job_run_details_policy ON cron.job_run_details USING (username OPERATOR(pg_catalog.=) current_user);

SELECT pg_catalog.pg_extension_config_dump('cron.job_run_details', '');
SELECT pg_catalog.pg_extension_config_dump('cron.runid_seq', '');

ALTER TABLE cron.job ADD COLUMN jobname name;

CREATE UNIQUE INDEX jobname_username_idx ON cron.job (jobname, username);
ALTER TABLE cron.job ADD CONSTRAINT jobname_username_uniq UNIQUE USING INDEX jobname_username_idx;

CREATE FUNCTION cron.schedule(job_name name, schedule text, command text)
    RETURNS bigint
    LANGUAGE C STRICT
    AS 'MODULE_PATHNAME', $$cron_schedule_named$$;
COMMENT ON FUNCTION cron.schedule(name,text,text)
    IS 'schedule a pg_cron job';

CREATE FUNCTION cron.unschedule(job_name name)
    RETURNS bool
    LANGUAGE C STRICT
    AS 'MODULE_PATHNAME', $$cron_unschedule_named$$;
COMMENT ON FUNCTION cron.unschedule(name)
    IS 'unschedule a pg_cron job';
