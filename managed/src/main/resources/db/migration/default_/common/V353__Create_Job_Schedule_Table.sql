-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS job_schedule(
  uuid                      UUID NOT NULL,
  customer_uuid             UUID NOT NULL,
  name                      VARCHAR(200) NOT NULL,
  last_job_instance_uuid    UUID,
  last_start_time           TIMESTAMP,
  last_end_time             TIMESTAMP,
  next_start_time           TIMESTAMP NOT NULL,
  execution_count           BIGINT default 0 NOT NULL,
  failed_count              BIGINT default 0 NOT NULL,
  state                     VARCHAR(20) NOT NULL,
  schedule_config           TEXT NOT NULL,
  job_config                TEXT NOT NULL,
  created_at                TIMESTAMP DEFAULT (CURRENT_TIMESTAMP AT TIME ZONE 'utc') NOT NULL,
  updated_at                TIMESTAMP DEFAULT (CURRENT_TIMESTAMP AT TIME ZONE 'utc') NOT NULL,
  CONSTRAINT pk_job_schedule primary key (uuid),
  CONSTRAINT fk_job_schedule_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE,
  CONSTRAINT uq_job_schedule_name UNIQUE (name, customer_uuid)
);

DROP INDEX IF EXISTS ix_job_schedule_customer_name;
CREATE INDEX ix_job_schedule_customer_name on job_schedule (customer_uuid, name);

DROP INDEX IF EXISTS ix_job_schedule_next_start_time;
CREATE INDEX ix_job_schedule_next_start_time on job_schedule (next_start_time);

CREATE TABLE IF NOT EXISTS job_instance(
  uuid                  UUID NOT NULL,
  job_schedule_uuid     UUID NOT NULL,
  start_time            TIMESTAMP NOT NULL,
  end_time              TIMESTAMP,
  state                 VARCHAR(20) NOT NULL,
  created_at            TIMESTAMP DEFAULT (CURRENT_TIMESTAMP AT TIME ZONE 'utc') NOT NULL,
  CONSTRAINT pk_job_instance primary key (uuid),
  CONSTRAINT fk_job_instance_job_schedule_uuid FOREIGN KEY (job_schedule_uuid) REFERENCES job_schedule(uuid) ON DELETE CASCADE
);

DROP INDEX IF EXISTS ix_job_instance_job_schedule;
CREATE INDEX ix_job_instance_job_schedule on job_instance (job_schedule_uuid);
