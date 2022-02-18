-- Copyright (c) YugaByte, Inc.

UPDATE alert_configuration SET threshold_unit = 'DAY'
 WHERE template = 'ENCRYPTION_AT_REST_CONFIG_EXPIRY';
