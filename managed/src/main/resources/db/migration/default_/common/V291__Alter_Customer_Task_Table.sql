-- Copyright (c) YugaByte, Inc.

CREATE INDEX ix_customer_task_task_uuid ON customer_task (customer_uuid, task_uuid);

CREATE INDEX ix_customer_task_target_uuid ON customer_task (target_uuid);


