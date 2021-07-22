-- Copyright (c) YugaByte, Inc.

-- No need to fill anything for h2.
alter table alert_definition_group drop constraint ck_adg_threshold_unit;
alter table alert_definition_group add constraint ck_adg_threshold_unit check (threshold_unit in ('STATUS','COUNT','PERCENT','MILLISECOND'));
