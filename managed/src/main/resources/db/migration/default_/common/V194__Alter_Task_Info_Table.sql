-- Copyright (c) YugaByte, Inc.

update task_info set task_type = 'UpdateUniverseYbcDetails' where task_type = 'UpdateYbcSoftwareVersion';
