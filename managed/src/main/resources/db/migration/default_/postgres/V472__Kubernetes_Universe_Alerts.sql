-- Copyright (c) YugabyteDB, Inc.

-- DB node CPU usage, Memory Consumption and DB node data disk usage now evaluate only VM
-- universes; Kubernetes universes get their own CPU, memory and data volume alerts. Each
-- customer's new configuration takes the settings and labels of its all-universe VM
-- configuration, preferring an active one, if it has one, and the VM template's custom labels.
-- The memory alert is opt-in, so only customers with such a configuration get it.

-- universeType is the template's universe type: 'K8S', 'VM' or null for any. As at runtime,
-- universes whose creation has not finished get no definitions, and customers without the
-- configuration are skipped.
DROP FUNCTION IF EXISTS create_universe_alert_definitions(text);
CREATE OR REPLACE FUNCTION create_universe_alert_definitions(
    configurationName text, universeType text DEFAULT NULL)
 RETURNS boolean
 language plpgsql
 as
$$
  declare
    customerRecord record;
    universeRecord record;
    configurationUuid uuid;
    definitionUuid uuid;
  begin
    for customerRecord in
      select * from customer
    loop
      configurationUuid :=  (select uuid from alert_configuration
        where name = configurationName and customer_uuid = customerRecord.uuid);
      if configurationUuid is null then
        continue;
      end if;
      for universeRecord in
        select * from universe where universe.customer_id = customerRecord.id
          and coalesce((universe_details_json::jsonb ->> 'creationSucceeded')::boolean, false)
          and (universeType is null
            or exists (
              select 1 from jsonb_array_elements(universe_details_json::jsonb -> 'clusters') cluster
              where cluster -> 'userIntent' ->> 'providerType' = 'kubernetes')
              = (universeType = 'K8S'))
      loop
        definitionUuid := gen_random_uuid();
        insert into alert_definition
          (uuid, customer_uuid, configuration_uuid)
        values
          (definitionUuid, customerRecord.uuid, configurationUuid);
        insert into alert_definition_label
          (definition_uuid, name, value)
        values
          (definitionUuid, 'source_type', 'universe'),
          (definitionUuid, 'source_name', universeRecord.name),
          (definitionUuid, 'source_uuid', universeRecord.universe_uuid),
          (definitionUuid, 'universe_name', universeRecord.name),
          (definitionUuid, 'universe_uuid', universeRecord.universe_uuid),
          (definitionUuid, 'node_prefix', universeRecord.universe_details_json::jsonb->>'nodePrefix');
      end loop;
    end loop;
    return TRUE;
  end;
$$;

create or replace function create_kubernetes_universe_alert_configuration(
    vmTemplate text, k8sTemplate text, configurationName text, configurationDescription text,
    defaultThresholds text, createWithoutVmConfiguration boolean)
 returns boolean
 language plpgsql
 as
$$
  declare
    customerRecord record;
    vmConfiguration record;
  begin
    for customerRecord in
      select * from customer
    loop
      select * into vmConfiguration from alert_configuration
        where customer_uuid = customerRecord.uuid and template = vmTemplate
          and target_type = 'UNIVERSE' and coalesce((target::jsonb ->> 'all')::boolean, false)
        order by active desc, create_time limit 1;
      if not found and not createWithoutVmConfiguration then
        continue;
      end if;
      insert into alert_template_settings (uuid, customer_uuid, template, create_time, labels)
        select gen_random_uuid(), customer_uuid, k8sTemplate, current_timestamp, labels
        from alert_template_settings
        where customer_uuid = customerRecord.uuid and template = vmTemplate
        on conflict on constraint uq_ats_customer_template do nothing;
      insert into alert_configuration
        (uuid, customer_uuid, name, description, create_time, target_type, target, thresholds,
         threshold_unit, template, duration_sec, active, default_destination, destination_uuid,
         labels)
      values
        (gen_random_uuid(), customerRecord.uuid, configurationName, configurationDescription,
         current_timestamp, 'UNIVERSE', '{"all":true}',
         coalesce(vmConfiguration.thresholds::text, defaultThresholds)::json_alias, 'PERCENT',
         k8sTemplate, coalesce(vmConfiguration.duration_sec, 0),
         coalesce(vmConfiguration.active, true),
         coalesce(vmConfiguration.default_destination, true),
         vmConfiguration.destination_uuid, vmConfiguration.labels);
    end loop;
    return TRUE;
  end;
$$;

select create_kubernetes_universe_alert_configuration(
  'NODE_CPU_USAGE', 'DB_K8S_CPU_USAGE', 'DB container CPU usage',
  'Average DB container CPU usage, as a percentage of the pod''s CPU request, is above threshold. '
    || 'Aggregation period is set via yb.alert.cpu_usage_interval_secs runtime property',
  '{"WARNING":{"condition":"GREATER_THAN", "threshold":90.0},'
    || '"SEVERE":{"condition":"GREATER_THAN", "threshold":95.0}}',
  true);
select create_universe_alert_definitions('DB container CPU usage', 'K8S');

select create_kubernetes_universe_alert_configuration(
  'MEMORY_CONSUMPTION', 'DB_K8S_MEMORY_USAGE', 'DB container memory usage',
  'DB container memory usage, excluding page cache, as a percentage of the container''s memory '
    || 'limit, averaged over 10 minutes, is above threshold',
  null,
  false);
select create_universe_alert_definitions('DB container memory usage', 'K8S');

select create_kubernetes_universe_alert_configuration(
  'NODE_DISK_USAGE', 'DB_K8S_DATA_VOLUME_USAGE', 'DB data volume usage',
  'DB pod data volume usage percentage is above threshold',
  '{"SEVERE":{"condition":"GREATER_THAN", "threshold":70.0}}',
  true);
select create_universe_alert_definitions('DB data volume usage', 'K8S');

-- The VM alerts' definitions on Kubernetes universes; their labels and alerts are deleted with
-- them, and the alert config writer removes their rule files.
delete from alert_definition definition
  using alert_configuration configuration, alert_definition_label label, universe
  where definition.configuration_uuid = configuration.uuid
    and configuration.template in ('NODE_CPU_USAGE', 'MEMORY_CONSUMPTION', 'NODE_DISK_USAGE')
    and label.definition_uuid = definition.uuid
    and label.name = 'universe_uuid'
    and label.value = universe.universe_uuid::text
    and exists (
      select 1 from jsonb_array_elements(universe.universe_details_json::jsonb -> 'clusters') cluster
      where cluster -> 'userIntent' ->> 'providerType' = 'kubernetes');

drop function create_kubernetes_universe_alert_configuration(
  text, text, text, text, text, boolean);
