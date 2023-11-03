-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS support_bundle (
  bundle_uuid                   uuid NOT NULL,
  path                          VARCHAR(255),
  scope_uuid                    uuid NOT NULL,
  start_date                    timestamp,
  end_date                      timestamp,
  bundle_details                JSON,
  constraint pk_support_bundle PRIMARY KEY (bundle_uuid)
);
