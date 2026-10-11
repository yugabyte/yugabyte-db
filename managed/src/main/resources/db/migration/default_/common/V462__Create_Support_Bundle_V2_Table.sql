-- Copyright (c) YugabyteDB, Inc.

CREATE TABLE IF NOT EXISTS support_bundle_v2 (
  bundle_uuid                   uuid NOT NULL,
  path                          VARCHAR(255),
  scope_uuid                    uuid,
  customer_uuid                 uuid,
  creation_date                 timestamp DEFAULT NOW() NOT NULL,
  start_date                    timestamp,
  end_date                      timestamp,
  bundle_details                JSON_ALIAS,
  status                        varchar(32) NOT NULL,
  constraint pk_support_bundle_v2 PRIMARY KEY (bundle_uuid)
);

ALTER TABLE support_bundle_v2 ADD CONSTRAINT fk_sb_v2_universe
  FOREIGN KEY (scope_uuid) REFERENCES universe (universe_uuid) ON DELETE CASCADE;

ALTER TABLE support_bundle_v2 ADD CONSTRAINT fk_sb_v2_customer
  FOREIGN KEY (customer_uuid) REFERENCES customer (uuid) ON DELETE CASCADE;

ALTER TABLE support_bundle_v2 ADD CONSTRAINT ck_support_bundle_v2_status
  CHECK (status IN ('Running','Success','Failed','Aborted'));

ALTER TABLE support_bundle_v2 ADD CONSTRAINT ck_support_bundle_v2_scope CHECK (
  (scope_uuid IS NOT NULL AND customer_uuid IS NULL)
  OR (scope_uuid IS NULL AND customer_uuid IS NOT NULL)
);
