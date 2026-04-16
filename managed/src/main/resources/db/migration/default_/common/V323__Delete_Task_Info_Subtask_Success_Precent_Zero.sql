-- Copyright (c) YugaByte, Inc.

-- Delete subtasks for CreateBackupSchedule where parent task is complete but subtasks are in 'Created' state.
DELETE FROM task_info t1 WHERE EXISTS 
        (SELECT 1 FROM task_info t2 WHERE t2.uuid = t1.parent_uuid and t2.task_type= 'CreateBackupSchedule' and t2.task_state='Success' and t2.parent_uuid is NULL) and t1.task_state='Created';
