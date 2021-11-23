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

import static com.yugabyte.yw.models.Alert.createQueryByFilter;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.SortBy;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.EntityOperation;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.paging.AlertPagedQuery;
import com.yugabyte.yw.models.paging.AlertPagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import io.ebean.Query;
import io.ebean.annotation.Transactional;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;

@Singleton
@Slf4j
public class AlertService {

  private final BeanValidator beanValidator;

  @Inject
  public AlertService(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
  }

  @Transactional
  public List<Alert> save(List<Alert> alerts) {
    if (CollectionUtils.isEmpty(alerts)) {
      return alerts;
    }

    List<Alert> beforeAlerts = Collections.emptyList();
    Set<UUID> alertUuids =
        alerts
            .stream()
            .filter(alert -> !alert.isNew())
            .map(Alert::getUuid)
            .collect(Collectors.toSet());
    if (!alertUuids.isEmpty()) {
      AlertFilter filter = AlertFilter.builder().uuids(alertUuids).build();
      beforeAlerts = list(filter);
    }
    Map<UUID, Alert> beforeAlertMap =
        beforeAlerts.stream().collect(Collectors.toMap(Alert::getUuid, Function.identity()));

    Map<EntityOperation, List<Alert>> toCreateAndUpdate =
        alerts
            .stream()
            .map(alert -> prepareForSave(alert, beforeAlertMap.get(alert.getUuid())))
            .filter(alert -> filterForSave(alert, beforeAlertMap.get(alert.getUuid())))
            .peek(alert -> validate(alert, beforeAlertMap.get(alert.getUuid())))
            .collect(Collectors.groupingBy(alert -> alert.isNew() ? CREATE : UPDATE));

    List<Alert> toCreate = toCreateAndUpdate.getOrDefault(CREATE, Collections.emptyList());
    if (!toCreate.isEmpty()) {
      toCreate.forEach(Alert::generateUUID);
      Alert.db().saveAll(toCreate);
    }

    List<Alert> toUpdate = toCreateAndUpdate.getOrDefault(UPDATE, Collections.emptyList());
    if (!toUpdate.isEmpty()) {
      Alert.db().updateAll(toUpdate);
    }

    log.trace("{} alerts saved", toCreate.size() + toUpdate.size());
    return alerts;
  }

  @Transactional
  public Alert save(Alert alert) {
    return save(Collections.singletonList(alert)).get(0);
  }

  public Alert get(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't get alert by null uuid");
    }
    return list(AlertFilter.builder().uuid(uuid).build()).stream().findFirst().orElse(null);
  }

  public Alert getOrBadRequest(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Alert UUID: " + uuid);
    }
    Alert alert = get(uuid);
    if (alert == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Alert UUID: " + uuid);
    }
    return alert;
  }

  @Transactional
  public List<Alert> markResolved(AlertFilter filter) {
    AlertFilter notResolved =
        filter.toBuilder().state(Alert.State.ACTIVE, Alert.State.ACKNOWLEDGED).build();
    List<Alert> resolved =
        list(notResolved)
            .stream()
            .peek(
                alert -> {
                  alert.setState(Alert.State.RESOLVED).setResolvedTime(nowWithoutMillis());
                  if (alert.getNotifiedState() != Alert.State.ACKNOWLEDGED) {
                    alert.setNextNotificationTime(nowWithoutMillis());
                  }
                })
            .collect(Collectors.toList());
    return save(resolved);
  }

  @Transactional
  public List<Alert> acknowledge(AlertFilter filter) {
    AlertFilter notResolved = filter.toBuilder().state(Alert.State.ACTIVE).build();
    List<Alert> resolved =
        list(notResolved)
            .stream()
            .map(
                alert ->
                    alert
                        .setState(Alert.State.ACKNOWLEDGED)
                        .setAcknowledgedTime(nowWithoutMillis())
                        .setNextNotificationTime(null)
                        .setNotifiedState(Alert.State.ACKNOWLEDGED))
            .collect(Collectors.toList());
    return save(resolved);
  }

  public List<Alert> list(AlertFilter filter) {
    return createQueryByFilter(filter).orderBy().desc("createTime").findList();
  }

  public int count(AlertFilter filter) {
    return createQueryByFilter(filter).findCount();
  }

  public AlertPagedResponse pagedList(AlertPagedQuery pagedQuery) {
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(SortBy.createTime);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    Query<Alert> query = Alert.createQueryByFilter(pagedQuery.getFilter()).query();
    return performPagedQuery(query, pagedQuery, AlertPagedResponse.class);
  }

  public List<Alert> listNotResolved(AlertFilter filter) {
    AlertFilter notResolved =
        filter.toBuilder().state(Alert.State.ACTIVE, Alert.State.ACKNOWLEDGED).build();
    return list(notResolved);
  }

  public List<UUID> listIds(AlertFilter filter) {
    return createQueryByFilter(filter).findIds();
  }

  public void process(AlertFilter filter, Consumer<Alert> consumer) {
    createQueryByFilter(filter).findEach(consumer);
  }

  @Transactional
  public void delete(UUID uuid) {
    Alert alert = get(uuid);
    if (alert == null) {
      log.warn("Alert {} is already deleted", uuid);
      return;
    }
    alert.delete();
    log.trace("Alert {} deleted", uuid);
  }

  @Transactional
  public int delete(AlertFilter filter) {
    int deleted = createQueryByFilter(filter).delete();
    log.trace("{} alerts deleted", deleted);
    return deleted;
  }

  /**
   * Required to make alert labels consistent between definition based alerts and manual alerts.
   * Will only need to insert definition related labels once all alerts will have underlying
   * definition.
   */
  private Alert prepareForSave(Alert alert, Alert before) {
    if (before != null) {
      alert.setCreateTime(before.getCreateTime());
    }
    return alert
        .setLabel(KnownAlertLabels.CUSTOMER_UUID, alert.getCustomerUUID().toString())
        .setLabel(KnownAlertLabels.SEVERITY, alert.getSeverity().name());
  }

  private boolean filterForSave(Alert alert, Alert before) {
    if (before == null) {
      return true;
    }
    return !alert.equals(before);
  }

  private void validate(Alert alert, Alert before) {
    beanValidator.validate(alert);
    if (before != null) {
      if (!alert.getCustomerUUID().equals(before.getCustomerUUID())) {
        beanValidator
            .error()
            .forField("customerUUID", "can't change for alert '" + alert.getUuid() + "'")
            .throwError();
      }
      if (before.getDefinitionUuid() != null
          && !alert.getDefinitionUuid().equals(before.getDefinitionUuid())) {
        beanValidator
            .error()
            .forField("definitionUuid", "can't change for alert '" + alert.getUuid() + "'")
            .throwError();
      }
      if (before.getConfigurationUuid() != null
          && !alert.getConfigurationUuid().equals(before.getConfigurationUuid())) {
        beanValidator
            .error()
            .forField("configurationUuid", "can't change for alert '" + alert.getUuid() + "'")
            .throwError();
      }
      if (!alert.getCreateTime().equals(before.getCreateTime())) {
        beanValidator
            .error()
            .forField("createTime", "can't change for alert '" + alert.getUuid() + "'")
            .throwError();
      }
    } else if (!alert.isNew()) {
      beanValidator
          .error()
          .forField("", "can't update missing alert '" + alert.getUuid() + "'")
          .throwError();
    }
  }
}
