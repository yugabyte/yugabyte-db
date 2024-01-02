-- Copyright (c) YugaByte, Inc.

-- Delete invalid subtasks from task_info before creating the contraint.
DELETE FROM task_info WHERE parent_uuid IS NOT NULL AND parent_uuid NOT IN
    (SELECT uuid FROM task_info WHERE parent_uuid IS NULL);

ALTER TABLE task_info ADD CONSTRAINT fk_task_info_parent_uuid FOREIGN KEY (parent_uuid)
    REFERENCES task_info (uuid) ON DELETE CASCADE ON UPDATE CASCADE;
