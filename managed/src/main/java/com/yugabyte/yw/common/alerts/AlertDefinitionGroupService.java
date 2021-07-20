/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts;

import static com.yugabyte.yw.models.AlertDefinitionGroup.createQueryByFilter;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertDefinitionGroupTarget;
import com.yugabyte.yw.models.AlertDefinitionGroupThreshold;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import com.yugabyte.yw.models.helpers.EntityOperation;
import com.yugabyte.yw.models.paging.AlertDefinitionGroupPagedQuery;
import com.yugabyte.yw.models.paging.AlertDefinitionGroupPagedResponse;
import io.ebean.Query;
import io.ebean.annotation.Transactional;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class AlertDefinitionGroupService {

  private final AlertDefinitionService alertDefinitionService;
  private final RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public AlertDefinitionGroupService(
      AlertDefinitionService alertDefinitionService, RuntimeConfigFactory runtimeConfigFactory) {
    this.alertDefinitionService = alertDefinitionService;
    this.runtimeConfigFactory = runtimeConfigFactory;
  }

  @Transactional
  public List<AlertDefinitionGroup> save(List<AlertDefinitionGroup> groups) {
    if (CollectionUtils.isEmpty(groups)) {
      return groups;
    }

    List<AlertDefinitionGroup> beforeGroups = Collections.emptyList();
    Set<UUID> groupUuids =
        groups
            .stream()
            .filter(group -> !group.isNew())
            .map(AlertDefinitionGroup::getUuid)
            .collect(Collectors.toSet());
    if (!groupUuids.isEmpty()) {
      AlertDefinitionGroupFilter filter =
          AlertDefinitionGroupFilter.builder().uuids(groupUuids).build();
      beforeGroups = list(filter);
    }
    Map<UUID, AlertDefinitionGroup> beforeGroupMap =
        beforeGroups
            .stream()
            .collect(Collectors.toMap(AlertDefinitionGroup::getUuid, Function.identity()));

    Map<EntityOperation, List<AlertDefinitionGroup>> toCreateAndUpdate =
        groups
            .stream()
            .peek(group -> validate(group, beforeGroupMap.get(group.getUuid())))
            .collect(Collectors.groupingBy(group -> group.isNew() ? CREATE : UPDATE));

    if (toCreateAndUpdate.containsKey(CREATE)) {
      List<AlertDefinitionGroup> toCreate = toCreateAndUpdate.get(CREATE);
      toCreate.forEach(group -> group.setCreateTime(nowWithoutMillis()));
      toCreate.forEach(AlertDefinitionGroup::generateUUID);
      AlertDefinitionGroup.db().saveAll(toCreate);
    }

    if (toCreateAndUpdate.containsKey(UPDATE)) {
      List<AlertDefinitionGroup> toUpdate = toCreateAndUpdate.get(UPDATE);
      AlertDefinitionGroup.db().updateAll(toUpdate);
    }

    manageDefinitions(groups, beforeGroups);

    log.debug("{} alert definition groups saved", groups.size());
    return groups;
  }

  @Transactional
  public AlertDefinitionGroup save(AlertDefinitionGroup definition) {
    return save(Collections.singletonList(definition)).get(0);
  }

  public AlertDefinitionGroup get(UUID uuid) {
    if (uuid == null) {
      throw new YWServiceException(BAD_REQUEST, "Can't get Alert Definition Group by null uuid");
    }
    return list(AlertDefinitionGroupFilter.builder().uuid(uuid).build())
        .stream()
        .findFirst()
        .orElse(null);
  }

  public AlertDefinitionGroup getOrBadRequest(UUID uuid) {
    if (uuid == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Definition Group UUID: " + uuid);
    }
    AlertDefinitionGroup group = get(uuid);
    if (group == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Definition Group UUID: " + uuid);
    }
    return group;
  }

  public List<AlertDefinitionGroup> list(AlertDefinitionGroupFilter filter) {
    return createQueryByFilter(filter).findList();
  }

  public AlertDefinitionGroupPagedResponse pagedList(AlertDefinitionGroupPagedQuery pagedQuery) {
    Query<AlertDefinitionGroup> query = createQueryByFilter(pagedQuery.getFilter()).query();
    return performPagedQuery(query, pagedQuery, AlertDefinitionGroupPagedResponse.class);
  }

  public List<UUID> listIds(AlertDefinitionGroupFilter filter) {
    return createQueryByFilter(filter).findIds();
  }

  public void process(AlertDefinitionGroupFilter filter, Consumer<AlertDefinitionGroup> consumer) {
    createQueryByFilter(filter).findEach(consumer);
  }

  @Transactional
  public void delete(UUID uuid) {
    AlertDefinitionGroupFilter filter = AlertDefinitionGroupFilter.builder().uuid(uuid).build();
    delete(filter);
  }

  @Transactional
  public void delete(Collection<AlertDefinitionGroup> groups) {
    if (CollectionUtils.isEmpty(groups)) {
      return;
    }
    AlertDefinitionGroupFilter filter =
        AlertDefinitionGroupFilter.builder()
            .uuids(groups.stream().map(AlertDefinitionGroup::getUuid).collect(Collectors.toSet()))
            .build();
    delete(filter);
  }

  public void delete(AlertDefinitionGroupFilter filter) {
    List<AlertDefinitionGroup> toDelete = list(filter);

    manageDefinitions(Collections.emptyList(), toDelete);

    int deleted = createQueryByFilter(filter).delete();
    log.debug("{} alert definition groups deleted", deleted);
  }

  private void validate(AlertDefinitionGroup group, AlertDefinitionGroup before) {
    if (group.getCustomerUUID() == null) {
      throw new YWServiceException(BAD_REQUEST, "Customer UUID field is mandatory");
    }
    if (StringUtils.isEmpty(group.getName())) {
      throw new YWServiceException(BAD_REQUEST, "Name field is mandatory");
    }
    if (group.getTargetType() == null) {
      throw new YWServiceException(BAD_REQUEST, "Target type field is mandatory");
    }
    if (group.getTarget() == null) {
      throw new YWServiceException(BAD_REQUEST, "Target field is mandatory");
    }
    AlertDefinitionGroupTarget target = group.getTarget();
    if (target.isAll() != CollectionUtils.isEmpty(target.getUuids())) {
      throw new YWServiceException(
          BAD_REQUEST, "Should select either all entries or particular UUIDs as target");
    }
    if (group.getTemplate() == null) {
      throw new YWServiceException(BAD_REQUEST, "Template field is mandatory");
    }
    if (MapUtils.isEmpty(group.getThresholds())) {
      throw new YWServiceException(BAD_REQUEST, "Query thresholds are mandatory");
    }
    if (group.getRouteUUID() != null
        && AlertRoute.get(group.getCustomerUUID(), group.getRouteUUID()) == null) {
      throw new YWServiceException(
          BAD_REQUEST, "Alert route " + group.getRouteUUID() + " is missing");
    }
    if (group.getThresholdUnit() == null) {
      throw new YWServiceException(BAD_REQUEST, "Threshold unit is mandatory");
    }
    if (group.getThresholdUnit() != group.getTemplate().getDefaultThresholdUnit()) {
      throw new YWServiceException(
          BAD_REQUEST, "Can't set threshold unit incompatible with alert definition template");
    }
    if (before != null) {
      if (!group.getCustomerUUID().equals(before.getCustomerUUID())) {
        throw new YWServiceException(
            BAD_REQUEST, "Can't change customer UUID for group " + group.getUuid());
      }
      if (!group.getTargetType().equals(before.getTargetType())) {
        throw new YWServiceException(
            BAD_REQUEST, "Can't change target type for group " + group.getUuid());
      }
    } else if (!group.isNew()) {
      throw new YWServiceException(BAD_REQUEST, "Can't update missing group " + group.getUuid());
    }
  }

  private void manageDefinitions(
      List<AlertDefinitionGroup> groups, List<AlertDefinitionGroup> beforeList) {
    Set<UUID> groupUUIDs =
        Stream.concat(groups.stream(), beforeList.stream())
            .map(AlertDefinitionGroup::getUuid)
            .collect(Collectors.toSet());

    if (groupUUIDs.isEmpty()) {
      return;
    }

    AlertDefinitionFilter filter = AlertDefinitionFilter.builder().groupUuids(groupUUIDs).build();
    Map<UUID, List<AlertDefinition>> definitionsByGroup =
        alertDefinitionService
            .list(filter)
            .stream()
            .collect(Collectors.groupingBy(AlertDefinition::getGroupUUID, Collectors.toList()));

    List<AlertDefinition> toSave = new ArrayList<>();
    List<AlertDefinition> toRemove = new ArrayList<>();

    Map<UUID, AlertDefinitionGroup> groupsMap =
        groups
            .stream()
            .collect(Collectors.toMap(AlertDefinitionGroup::getUuid, Function.identity()));
    for (UUID uuid : groupUUIDs) {
      AlertDefinitionGroup group = groupsMap.get(uuid);

      List<AlertDefinition> currentDefinitions =
          definitionsByGroup.getOrDefault(uuid, Collections.emptyList());
      if (group == null) {
        toRemove.addAll(currentDefinitions);
      } else {
        Customer customer = Customer.getOrBadRequest(group.getCustomerUUID());
        AlertDefinitionGroupTarget target = group.getTarget();
        switch (group.getTargetType()) {
          case CUSTOMER:
            if (currentDefinitions.size() > 1) {
              throw new IllegalStateException(
                  "More than one definition for CUSTOMER alert definition group " + uuid);
            }
            AlertDefinition definition;
            if (currentDefinitions.isEmpty()) {
              definition = createEmptyDefinition(group);
            } else {
              definition = currentDefinitions.get(0);
            }
            // For now it's just a query
            definition.setQuery(group.getTemplate().getTemplate());
            definition.setLabels(
                AlertDefinitionLabelsBuilder.create().appendTarget(customer).get());
            toSave.add(definition);
            break;
          case UNIVERSE:
            Set<UUID> universeUUIDs;
            Set<Universe> universes;

            if (target.isAll()) {
              universes = Universe.getAllWithoutResources(customer);
              universeUUIDs =
                  universes.stream().map(Universe::getUniverseUUID).collect(Collectors.toSet());
            } else {
              universeUUIDs =
                  Stream.concat(
                          currentDefinitions.stream().map(AlertDefinition::getUniverseUUID),
                          target.getUuids().stream())
                      .collect(Collectors.toSet());
              universes = Universe.getAllWithoutResources(universeUUIDs);
            }
            Map<UUID, Universe> universeMap =
                universes
                    .stream()
                    .collect(Collectors.toMap(Universe::getUniverseUUID, Function.identity()));
            Map<UUID, AlertDefinition> definitionsByUniverseUuid =
                currentDefinitions
                    .stream()
                    .collect(
                        Collectors.toMap(AlertDefinition::getUniverseUUID, Function.identity()));
            for (UUID universeUuid : universeUUIDs) {
              Universe universe = universeMap.get(universeUuid);
              AlertDefinition universeDefinition = definitionsByUniverseUuid.get(universeUuid);
              boolean shouldHaveDefinition =
                  (target.isAll() || target.getUuids().contains(universeUuid)) && universe != null;
              if (shouldHaveDefinition) {
                if (universeDefinition == null) {
                  universeDefinition = createEmptyDefinition(group);
                }
                universeDefinition.setConfigWritten(false);
                String nodePrefix = universe.getUniverseDetails().nodePrefix;
                universeDefinition.setQuery(group.getTemplate().buildTemplate(nodePrefix));
                universeDefinition.setLabels(
                    AlertDefinitionLabelsBuilder.create().appendTarget(universe).get());
                toSave.add(universeDefinition);
              } else if (universeDefinition != null) {
                toRemove.add(universeDefinition);
              }
            }
            break;
          default:
            throw new IllegalStateException("Unexpected target type " + group.getTargetType());
        }
      }
    }

    if (!toSave.isEmpty()) {
      alertDefinitionService.save(toSave);
    }
    if (!toRemove.isEmpty()) {
      Set<UUID> uuids = toRemove.stream().map(AlertDefinition::getUuid).collect(Collectors.toSet());
      alertDefinitionService.delete(AlertDefinitionFilter.builder().uuids(uuids).build());
    }
  }

  public AlertDefinitionGroup createGroupFromTemplate(
      Customer customer, AlertDefinitionTemplate template) {
    return new AlertDefinitionGroup()
        .setCustomerUUID(customer.getUuid())
        .setName(template.getName())
        .setDescription(template.getDescription())
        .setTargetType(template.getTargetType())
        .setTarget(new AlertDefinitionGroupTarget().setAll(true))
        .setThresholds(
            template
                .getDefaultThresholdParamMap()
                .entrySet()
                .stream()
                .collect(
                    Collectors.toMap(
                        Map.Entry::getKey,
                        e ->
                            new AlertDefinitionGroupThreshold()
                                .setCondition(template.getDefaultThresholdCondition())
                                .setThreshold(
                                    runtimeConfigFactory
                                        .globalRuntimeConf()
                                        .getDouble(e.getValue())))))
        .setThresholdUnit(template.getDefaultThresholdUnit())
        .setTemplate(template)
        .setDurationSec(template.getDefaultDurationSec());
  }

  private AlertDefinition createEmptyDefinition(AlertDefinitionGroup group) {
    return new AlertDefinition()
        .setCustomerUUID(group.getCustomerUUID())
        .setGroupUUID(group.getUuid());
  }
}
