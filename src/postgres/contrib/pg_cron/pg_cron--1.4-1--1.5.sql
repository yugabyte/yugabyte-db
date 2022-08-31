/* pg_cron--1.4-1--1.5.sql */

-- cron.job should only have 1 tablet so that we have an easily identifiable tablet that we can base
-- the the cron leadership on
DROP TABLE cron.job;
CREATE TABLE cron.job (
  jobid bigint primary key default nextval('cron.jobid_seq'),
  schedule text not null,
  command text not null,
  nodename text,
  nodeport int,
  database text not null,
  username text not null default current_user,
    active boolean not null default 'true',
    jobname name
) SPLIT INTO 1 tablets;

CREATE UNIQUE INDEX jobname_username_idx ON cron.job (jobname, username);
ALTER TABLE cron.job ADD CONSTRAINT jobname_username_uniq UNIQUE USING INDEX jobname_username_idx;

----------

ALTER TABLE cron.job_run_details
    ADD COLUMN nodename text NOT NULL;

----------

DROP FUNCTION cron.schedule(text,text,text);
CREATE FUNCTION cron.schedule(job_name text,
                              schedule text,
                               command text,
                              nodename text default null)
RETURNS bigint
LANGUAGE C
AS 'MODULE_PATHNAME', $$cron_schedule_named$$;

COMMENT ON FUNCTION cron.schedule(text,text,text,text)
IS 'schedule a pg_cron job';

----------

DROP FUNCTION cron.alter_job(bigint,text,text,text,text,boolean);
CREATE FUNCTION cron.alter_job(job_id bigint,
                schedule text default null,
                command text default null,
                database text default null,
                username text default null,
                active boolean default null,
                                nodename text default null)
RETURNS void
LANGUAGE C
AS 'MODULE_PATHNAME', $$cron_alter_job$$;

COMMENT ON FUNCTION cron.alter_job(bigint,text,text,text,text,boolean,text)
IS 'Alter the job identified by job_id. Any option left as NULL will not be modified.';

/* admin should decide whether alter_job is safe by explicitly granting execute */
REVOKE ALL ON FUNCTION cron.alter_job(bigint,text,text,text,text,boolean, text) FROM public;

----------

DROP FUNCTION cron.schedule_in_database(text,text,text,text,text,boolean);
CREATE FUNCTION cron.schedule_in_database(job_name text,
                      schedule text,
                      command text,
                      database text,
                      username text default null,
                      active boolean default 'true',
                                          nodename text default null)
RETURNS bigint
LANGUAGE C
AS 'MODULE_PATHNAME', $$cron_schedule_in_database$$;

COMMENT ON FUNCTION cron.schedule_in_database(text,text,text,text,text,boolean,text)
IS 'schedule a pg_cron job';

/* admin should decide whether cron.schedule_in_database is safe by explicitly granting execute */
REVOKE ALL ON FUNCTION cron.schedule_in_database(text,text,text,text,text,boolean,text) FROM public;

----------

CREATE FUNCTION cron.job_cache_invalidate_non_trigger()
    RETURNS bool
    LANGUAGE C
    AS 'MODULE_PATHNAME', $$cron_job_cache_invalidate_non_trigger$$;
COMMENT ON FUNCTION cron.job_cache_invalidate_non_trigger()
    IS 'invalidate job cache.';

----------

CREATE FUNCTION cron.signal_job_run_change()
    RETURNS bool
    LANGUAGE C
    AS 'MODULE_PATHNAME', $$cron_signal_job_run_change$$;
COMMENT ON FUNCTION cron.signal_job_run_change()
    IS 'signal new job assigned.';

----------
