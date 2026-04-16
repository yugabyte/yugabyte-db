-- Copyright (c) YugaByte, Inc.

-- Modify duplicate config names per customer uuid by appending a unique suffix to it's end.
CREATE VIEW helper AS 
    SELECT config_uuid, row_number() OVER (
            PARTITION BY customer_config.customer_uuid, customer_config.config_name 
            ORDER BY config_uuid
        ) row_number FROM customer_config;

UPDATE customer_config
SET config_name = config_name || CASE WHEN (
    select row_number from helper where helper.config_uuid=customer_config.config_uuid) = 1 THEN '' 
    ELSE ('-edited-' || (
        (select row_number from helper where helper.config_uuid=customer_config.config_uuid)-1)::TEXT
    ) END;

DROP VIEW helper;

-- create unique contraint on customer_uuid and config_name.
ALTER TABLE customer_config DROP CONSTRAINT IF EXISTS config_name_unique;
ALTER TABLE customer_config ADD CONSTRAINT config_name_unique UNIQUE (customer_uuid, config_name);
