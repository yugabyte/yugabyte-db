-- Copyright (c) YugaByte, Inc.

ALTER TABLE instance_type RENAME TO instance_type_old;

CREATE TABLE instance_type
(
    provider_uuid              UUID                 NOT NULL,
    instance_type_code         VARCHAR(255)         NOT NULL,
    active                     BOOLEAN DEFAULT TRUE NOT NULL,
    num_cores                  FLOAT                NOT NULL,
    mem_size_gb                FLOAT                NOT NULL,
    instance_type_details_json TEXT,
    CONSTRAINT instance_type_provider_uuid_fkey FOREIGN KEY (provider_uuid) REFERENCES provider (uuid) ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT instance_type_pkey PRIMARY KEY (provider_uuid, instance_type_code)
);

INSERT INTO instance_type
SELECT p.uuid, i.instance_type_code, i.active, i.num_cores, i.mem_size_gb, i.instance_type_details_json
FROM provider p
         INNER JOIN instance_type_old i
                    ON i.provider_code = p.code;

DROP TABLE instance_type_old;
