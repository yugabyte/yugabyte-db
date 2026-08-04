-- Copyright (c) YugaByte, Inc.
ALTER TABLE metric ADD COLUMN IF NOT EXISTS target_labels varchar(4000);
ALTER TABLE metric_label ADD COLUMN IF NOT EXISTS target_label boolean DEFAULT false NOT NULL;
ALTER TABLE metric DROP CONSTRAINT IF EXISTS uq_name_customer_target;
ALTER TABLE metric ADD CONSTRAINT uq_name_customer_target UNIQUE (name, customer_uuid, target_uuid, target_labels);
