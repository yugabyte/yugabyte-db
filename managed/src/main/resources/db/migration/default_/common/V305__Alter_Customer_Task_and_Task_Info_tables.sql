-- Copyright (c) YugaByte, Inc.

-- Add unique constraint(task_uuid) to customer_task
ALTER TABLE customer_task ADD CONSTRAINT uk_task_uuid UNIQUE (task_uuid);

-- Delete invalid tasks from customer_task before creating the constraint.
DELETE FROM customer_task WHERE NOT EXISTS (SELECT 1 FROM customer WHERE customer.uuid = customer_task.customer_uuid);

-- Delete invalid tasks from task_info before creating the constraint.
DELETE FROM task_info WHERE NOT EXISTS (SELECT 1 FROM customer_task WHERE customer_task.task_uuid = task_info.uuid) AND task_info.parent_uuid IS NULL;

-- Add a foreign key constraint to customer_task with ON DELETE CASCADE
ALTER TABLE customer_task ADD CONSTRAINT fk_customer_task_task_info FOREIGN KEY (task_uuid) REFERENCES task_info (uuid) ON DELETE CASCADE;
