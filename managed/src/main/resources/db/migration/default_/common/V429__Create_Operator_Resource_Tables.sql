-- Copyright (c) YugaByte, Inc.

CREATE TABLE IF NOT EXISTS operator_resource (
  name                          VARCHAR(255) NOT NULL,
  data                          TEXT,
  CONSTRAINT pk_operator_resource PRIMARY KEY (name)
);

CREATE TABLE IF NOT EXISTS operator_resource_dependency (
  resource_name                 VARCHAR(255) NOT NULL,
  dependent_name                VARCHAR(255) NOT NULL,
  CONSTRAINT pk_operator_resource_dependency PRIMARY KEY (resource_name, dependent_name),
  CONSTRAINT fk_ord_resource FOREIGN KEY (resource_name)
    REFERENCES operator_resource(name) ON DELETE CASCADE,
  CONSTRAINT fk_ord_dependent FOREIGN KEY (dependent_name)
    REFERENCES operator_resource(name) ON DELETE CASCADE
);
