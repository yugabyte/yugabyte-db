-- Copyright (c) YugaByte, Inc.
UPDATE alert SET next_notification_time = current_timestamp
  WHERE next_notification_time is null
    AND target_state in ('ACTIVE','RESOLVED')
    AND state != target_state
    AND notified_state != 'ACKNOWLEDGED';
UPDATE alert SET state = target_state;
ALTER TABLE alert DROP CONSTRAINT IF EXISTS ck_alert_state;
ALTER TABLE alert ADD CONSTRAINT ck_alert_state CHECK ( state in ('ACTIVE','ACKNOWLEDGED','RESOLVED'));
ALTER TABLE alert DROP COLUMN IF EXISTS target_state;

