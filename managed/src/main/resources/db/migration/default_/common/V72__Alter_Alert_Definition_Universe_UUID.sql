-- Copyright (c) YugaByte, Inc.
insert into alert_definition_label (definition_uuid, name, value)
select uuid, 'universe_uuid', cast(universe_uuid as text) from alert_definition;

insert into alert_definition_label (definition_uuid, name, value)
select ad.uuid, 'universe_name', u.name from alert_definition as ad
join universe as u on u.universe_uuid = ad.universe_uuid;

alter table alert_definition alter column universe_uuid drop not null;
update alert_definition set universe_uuid = null;
