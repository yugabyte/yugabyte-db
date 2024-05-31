-- Copyright (c) YugaByte, Inc.

-- Modify duplicate certificate labels per customer uuid by appending a unique suffix to it's end.
CREATE VIEW helper AS 
    SELECT uuid, label, row_number() OVER (
            PARTITION BY certificate_info.customer_uuid, certificate_info.label 
            ORDER BY uuid
        ) AS row_number FROM certificate_info;

UPDATE certificate_info
    SET label = label || ( '-edited-' || ((SELECT row_number FROM helper WHERE helper.uuid=certificate_info.uuid) - 1) :: TEXT)
    WHERE exists (SELECT * FROM helper WHERE helper.label=certificate_info.label AND helper.row_number = 2);

DROP VIEW helper;

-- create unique contraint on customer_uuid and label.
ALTER TABLE certificate_info ADD CONSTRAINT unique_label_per_customer UNIQUE (customer_uuid, label);
