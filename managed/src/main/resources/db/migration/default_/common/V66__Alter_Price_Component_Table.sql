--  Copyright (c) YugaByte, Inc.

ALTER TABLE price_component
    RENAME TO price_component_old;

CREATE TABLE price_component
(
    provider_uuid      UUID         NOT NULL,
    region_code        VARCHAR(255) NOT NULL,
    component_code     VARCHAR(255) NOT NULL,
    price_details_json TEXT         NOT NULL,
    CONSTRAINT price_component_provider_uuid_fkey FOREIGN KEY (provider_uuid) REFERENCES provider (uuid) ON DELETE CASCADE ON UPDATE CASCADE,
    CONSTRAINT price_component_pkey
        PRIMARY KEY (provider_uuid, region_code, component_code)
);

INSERT INTO price_component
SELECT p.uuid, pc.region_code, pc.component_code, pc.price_details_json
FROM provider p
         INNER JOIN price_component_old pc
                    ON pc.provider_code = p.code;

DROP TABLE price_component_old;
