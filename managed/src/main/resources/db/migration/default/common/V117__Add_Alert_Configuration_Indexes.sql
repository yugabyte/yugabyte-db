-- Copyright (c) YugaByte, Inc.

drop index if exists ix_alert_configuration_customer_target_type;
create index ix_alert_configuration_customer_target_type on alert_configuration (customer_uuid, target_type);
drop index if exists ix_alert_configuration_customer_name;
create index ix_alert_configuration_customer_name on alert_configuration (customer_uuid, name);
drop index if exists ix_alert_configuration_customer_template;
create index ix_alert_configuration_customer_template on alert_configuration (customer_uuid, template);
drop index if exists ix_alert_configuration_destination;
create index ix_alert_configuration_destination on alert_configuration (destination_uuid);
