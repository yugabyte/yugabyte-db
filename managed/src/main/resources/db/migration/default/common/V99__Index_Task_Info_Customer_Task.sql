--  Copyright (c) YugaByte, Inc.

CREATE INDEX IF NOT EXISTS task_info_parent_uuid_idx ON task_info (parent_uuid);
CREATE INDEX IF NOT EXISTS customer_task_task_uuid_idx ON customer_task (task_uuid);
