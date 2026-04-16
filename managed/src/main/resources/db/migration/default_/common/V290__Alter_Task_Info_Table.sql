-- Copyright (c) YugaByte, Inc.

-- Delete invalid subtasks from task_info before creating the contraint.
DELETE FROM task_info t1 WHERE NOT EXISTS (SELECT 1 FROM task_info t2 WHERE
    t1.parent_uuid = t2.uuid) AND t1.parent_uuid IS NOT NULL;

ALTER TABLE task_info ADD CONSTRAINT fk_task_info_parent_uuid FOREIGN KEY (parent_uuid)
    REFERENCES task_info (uuid) ON DELETE CASCADE ON UPDATE CASCADE;
