-- Copyright (c) YugaByte, Inc.
delete from alert_label;

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'definition_uuid', cast(d.uuid as text) from alert a
join alert_definition d on a.definition_uuid = d.uuid);

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'definition_name', d.name from alert a
join alert_definition d on a.definition_uuid = d.uuid);

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'definition_active', cast(d.active as text) from alert a
join alert_definition d on a.definition_uuid = d.uuid);

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'universe_uuid', cast(u.universe_uuid as text) from alert a
join universe u on a.target_uuid = u.universe_uuid
 where a.target_type in ('UniverseType', 'ClusterType', 'TableType', 'NodeType'));

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'universe_name', u.name from alert a
join universe u on a.target_uuid = u.universe_uuid
 where a.target_type in ('UniverseType', 'ClusterType', 'TableType', 'NodeType'));

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'customer_uuid', cast(a.customer_uuid as text) from alert a);

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'target_uuid', cast(u.universe_uuid as text) from alert a
join universe u on a.target_uuid = u.universe_uuid
 where a.target_type in ('UniverseType', 'ClusterType', 'TableType', 'NodeType'));

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'target_name', u.name from alert a
join universe u on a.target_uuid = u.universe_uuid
 where a.target_type in ('UniverseType', 'ClusterType', 'TableType', 'NodeType'));

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'target_type', 'universe' from alert a
join universe u on a.target_uuid = u.universe_uuid
 where a.target_type in ('UniverseType', 'ClusterType', 'TableType', 'NodeType'));

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'target_uuid', cast(c.uuid as text) from alert a
join customer c on a.customer_uuid = c.uuid
 where a.target_type not in ('UniverseType', 'ClusterType', 'TableType', 'NodeType'));

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'target_name', c.name from alert a
join customer c on a.customer_uuid = c.uuid
 where a.target_type not in ('UniverseType', 'ClusterType', 'TableType', 'NodeType'));

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'target_type', 'customer' from alert a
join customer c on a.customer_uuid = c.uuid
 where a.target_type not in ('UniverseType', 'ClusterType', 'TableType', 'NodeType'));

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'error_code', a.err_code from alert a);

insert into alert_label (alert_uuid, name, value)
(select a.uuid, 'alert_type', a.type from alert a);

alter table alert drop column if exists target_type;
alter table alert drop column if exists target_uuid;