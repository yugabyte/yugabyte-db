-- Copyright (c) YugaByte, Inc.

DROP INDEX IF EXISTS ix_job_schedule_customer_job_config_classname;
CREATE INDEX ix_job_schedule_customer_job_config_classname on job_schedule ((job_config::jsonb->>'classname'));
