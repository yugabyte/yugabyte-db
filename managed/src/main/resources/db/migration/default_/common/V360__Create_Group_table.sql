-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF not EXISTS groups_mapping_info (
    uuid UUID NOT NULL,
    identifier VARCHAR(255) NOT NULL,
    role_uuid UUID NOT NULL,
    customer_uuid UUID NOT NULL,
    type VARCHAR(5) NOT NULL,
    
    CONSTRAINT fk_groups_info_customer_uuid foreign key (customer_uuid) references customer (uuid) ON UPDATE CASCADE ON DELETE CASCADE,
    CONSTRAINT fk_groups_info_role_uuid foreign key (role_uuid) references role (role_uuid),
    CONSTRAINT ck_groups_info_type check(type in ('LDAP', 'OIDC')),
    CONSTRAINT pk_group_mapping_info PRIMARY KEY (uuid),
    CONSTRAINT uq_groups_info_identifier UNIQUE (identifier)
);

CREATE TABLE IF NOT EXISTS principal (
  -- uuid will be same as user/group uuid
  uuid UUID NOT NULL,
  user_uuid UUID,
  group_uuid UUID,
  type VARCHAR(10) NOT NULL,

  CONSTRAINT pk_principal PRIMARY KEY (uuid),
  CONSTRAINT fk_principal_user foreign key (user_uuid) references users (uuid) ON UPDATE CASCADE ON DELETE CASCADE,
  CONSTRAINT fk_principal_group foreign key (group_uuid) references groups_mapping_info (uuid) ON UPDATE CASCADE ON DELETE CASCADE,

  CONSTRAINT ck_type check(type in ('USER', 'LDAP_GROUP', 'OIDC_GROUP')),
  -- CHECK to make sure only one of them is non null
  CHECK (
        (user_uuid IS NOT NULL)::INTEGER +
        (group_uuid IS NOT NULL)::INTEGER = 1)
);