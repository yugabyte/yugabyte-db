ALTER TABLE backup
ADD COLUMN IF NOT EXISTS schedule_name character varying(255);

UPDATE backup
SET (schedule_name) = (select schedule_name
FROM schedule
WHERE backup.schedule_uuid = schedule.schedule_uuid);