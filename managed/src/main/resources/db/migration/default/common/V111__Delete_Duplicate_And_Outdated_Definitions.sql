-- Copyright (c) YugaByte, Inc.

delete from alert_definition where uuid in
  (select uuid from
     (select
        uuid,
        group_uuid,
        adl.value,
        row_number() over(partition by group_uuid, adl.value order by uuid) as rn
      from alert_definition ad join alert_definition_label adl on ad.uuid = adl.definition_uuid
      where adl.name = 'universe_uuid') as ranked
   where rn > 1);

delete from alert_definition where uuid in
  (select definition_uuid
   from alert_definition_label
   where name = 'universe_uuid' and value::uuid not in (select universe_uuid from universe));
