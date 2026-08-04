-- Copyright (c) YugaByte, Inc.

UPDATE task_info SET sub_task_group_type = 'Configuring' WHERE sub_task_group_type = 'Invalid';
