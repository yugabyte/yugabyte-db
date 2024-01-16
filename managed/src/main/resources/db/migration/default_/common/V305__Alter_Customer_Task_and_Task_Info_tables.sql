-- Copyright (c) YugaByte, Inc.

-- Add unique constraint(task_uuid) to customer_task
ALTER TABLE customer_task ADD CONSTRAINT uk_task_uuid UNIQUE (task_uuid);

-- Delete invalid tasks from customer_task before creating the constraint.
DELETE FROM customer_task WHERE customer_uuid NOT IN (SELECT uuid FROM customer);

-- Delete invalid tasks from task_info before creating the constraint.
DELETE FROM task_info WHERE uuid NOT IN (SELECT task_uuid FROM customer_task);

-- Add a foreign key constraint to customer_task with ON DELETE CASCADE
ALTER TABLE customer_task ADD CONSTRAINT fk_customer_task_task_info FOREIGN KEY (task_uuid) REFERENCES task_info (uuid) ON DELETE CASCADE;
