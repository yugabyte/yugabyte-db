-- Copyright (c) YugaByte, Inc.

drop index if exists ix_alert_definition_configuration_uuid;
create index ix_alert_definition_configuration_uuid on alert_definition (configuration_uuid);
