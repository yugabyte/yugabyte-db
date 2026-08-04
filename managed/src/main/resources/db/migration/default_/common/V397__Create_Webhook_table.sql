-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF not EXISTS webhook (
    uuid UUID not null,
    url VARCHAR(1024) NOT NULL,
    dr_config_uuid UUID,
    CONSTRAINT pk_webhook PRIMARY KEY (uuid),
    CONSTRAINT fk_webhook_dr_config_uuid FOREIGN KEY (dr_config_uuid) REFERENCES dr_config(uuid) ON UPDATE CASCADE ON DELETE CASCADE
);
