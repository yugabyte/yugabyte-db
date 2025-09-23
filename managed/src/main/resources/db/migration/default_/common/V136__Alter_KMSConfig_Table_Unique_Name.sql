 -- Copyright (c) YugaByte, Inc.

 -- Modify duplicate kms_config names per customer uuid by appending a unique suffix to it's end.
 CREATE VIEW helper AS 
     SELECT config_uuid, name, row_number() OVER (
             PARTITION BY kms_config.customer_uuid, kms_config.name 
             ORDER BY config_uuid
         ) AS row_number  FROM kms_config;

UPDATE kms_config
 SET name = name || ( '-edited-' || ((SELECT row_number FROM helper WHERE helper.config_uuid=kms_config.config_uuid) - 1) :: TEXT)
 WHERE exists (SELECT * FROM helper WHERE helper.name=kms_config.name AND helper.row_number = 2);

 DROP VIEW helper;


 -- create unique contraint on customer_uuid and name.
 ALTER TABLE kms_config DROP CONSTRAINT IF EXISTS unique_name_per_customer;
 ALTER TABLE kms_config ADD CONSTRAINT unique_name_per_customer UNIQUE (customer_uuid, name);