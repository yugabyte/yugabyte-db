-- Copyright (c) YugaByte, Inc.

UPDATE alert_definition
SET config_written = false
WHERE configuration_uuid IN
  (select uuid from alert_configuration where template in
     ('YSQL_OP_P99_LATENCY', 'YSQL_OP_AVG_LATENCY', 'YCQL_OP_AVG_LATENCY', 'YCQL_OP_P99_LATENCY',
      'TABLET_SERVER_AVG_READ_LATENCY', 'TABLET_SERVER_AVG_WRITE_LATENCY'));
