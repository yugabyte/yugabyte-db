-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS universe_perf_advisor_run(
  uuid                  UUID NOT NULL,
  customer_uuid         UUID NOT NULL,
  universe_uuid         UUID NOT NULL,
  state                 VARCHAR(100) NOT NULL,
  schedule_time         TIMESTAMP NOT NULL,
  start_time            TIMESTAMP,
  end_time              TIMESTAMP,
  manual                BOOLEAN NOT NULL,
  CONSTRAINT pk_universe_perf_advisor_run primary key (uuid),
  CONSTRAINT fk_upar_customer_uuid FOREIGN KEY (customer_uuid) REFERENCES customer(uuid) ON DELETE CASCADE,
  CONSTRAINT fk_upar_universe_uuid FOREIGN KEY (universe_uuid) REFERENCES universe(universe_uuid) ON DELETE CASCADE,
  CONSTRAINT ck_upar_state CHECK (state in ('PENDING','RUNNING','COMPLETED','FAILED'))
)
