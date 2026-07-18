-- Copyright (c) YugaByte, Inc.

UPDATE high_availability_config
SET accept_any_certificate = COALESCE(
  CAST((SELECT encode(value, 'escape')
        FROM runtime_config_entry
        WHERE path='yb.ha.ws.ssl.loose.acceptAnyCertificate' LIMIT 1) AS BOOLEAN),
  false
);