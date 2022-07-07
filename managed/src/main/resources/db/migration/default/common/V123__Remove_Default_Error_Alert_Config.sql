-- Copyright (c) YugaByte, Inc.

delete from alert_configuration where template = 'DB_ERROR_LOGS' and name = 'DB error logs';
