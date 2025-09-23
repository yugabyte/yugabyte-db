-- Copyright (c) YugaByte, Inc.

alter table alert_configuration drop constraint ck_ac_threshold_unit;
alter table alert_configuration add constraint ck_ac_threshold_unit check (threshold_unit in ('STATUS','COUNT','PERCENT','MILLISECOND','SECOND','DAY','MEGABYTE'));
