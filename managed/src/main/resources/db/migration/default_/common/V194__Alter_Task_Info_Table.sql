-- Copyright (c) YugabyteDB, Inc.

update task_info set task_type = 'UpdateUniverseYbcDetails' where task_type = 'UpdateYbcSoftwareVersion';
