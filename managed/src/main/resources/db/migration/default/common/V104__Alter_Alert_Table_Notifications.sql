-- Copyright (c) YugaByte, Inc.
ALTER TABLE alert ADD COLUMN IF NOT EXISTS notification_attempt_time timestamp;
ALTER TABLE alert ADD COLUMN IF NOT EXISTS next_notification_time timestamp;
ALTER TABLE alert ADD COLUMN IF NOT EXISTS notifications_failed int not null default 0;
ALTER TABLE alert ADD COLUMN IF NOT EXISTS notified_state varchar(20);

ALTER TABLE alert DROP CONSTRAINT IF EXISTS ck_alert_notified_state;
ALTER TABLE alert ADD CONSTRAINT ck_alert_notified_state CHECK ( notified_state in ('ACTIVE','ACKNOWLEDGED','RESOLVED'));
CREATE INDEX IF NOT EXISTS ix_alert_notification_time on alert (next_notification_time);
