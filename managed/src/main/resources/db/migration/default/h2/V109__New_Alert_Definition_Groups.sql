-- Copyright (c) YugaByte, Inc.

alter table alert_definition_group drop constraint ck_adg_threshold_unit;
alter table alert_definition_group add constraint ck_adg_threshold_unit check (threshold_unit in ('STATUS','COUNT','PERCENT','MILLISECOND','SECOND','DAY'));
