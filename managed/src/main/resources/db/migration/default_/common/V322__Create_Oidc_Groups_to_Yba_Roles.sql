-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF not EXISTS oidc_group_to_yba_roles (
    uuid UUID NOT NULL,
    group_name VARCHAR(255) NOT NULL,
    yba_roles UUID ARRAY NOT NULL,
    customer_uuid UUID NOT NULL,
    
    CONSTRAINT pk_oidc_group_to_yba_roles PRIMARY KEY (uuid),
    CONSTRAINT uq_group_name UNIQUE (group_name)
);