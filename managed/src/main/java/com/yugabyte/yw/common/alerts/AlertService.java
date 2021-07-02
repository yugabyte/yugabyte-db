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

import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.EntityOperation;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.ebean.annotation.Transactional;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

import javax.inject.Singleton;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Collectors;

import static com.yugabyte.yw.models.Alert.createQueryByFilter;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static play.mvc.Http.Status.BAD_REQUEST;

@Singleton
@Slf4j
public class AlertService {

  @Transactional
  public List<Alert> save(List<Alert> alerts) {
    if (CollectionUtils.isEmpty(alerts)) {
      return alerts;
    }

    Map<EntityOperation, List<Alert>> toCreateAndUpdate =
        alerts
            .stream()
            .map(this::prepareForSave)
            .peek(this::validate)
            .collect(Collectors.groupingBy(alert -> alert.isNew() ? CREATE : UPDATE));

    if (toCreateAndUpdate.containsKey(CREATE)) {
      List<Alert> toCreate = toCreateAndUpdate.get(CREATE);
      toCreate.forEach(Alert::generateUUID);
      Alert.db().saveAll(toCreate);
    }

    if (toCreateAndUpdate.containsKey(UPDATE)) {
      List<Alert> toUpdate = toCreateAndUpdate.get(UPDATE);
      Alert.db().updateAll(toUpdate);
    }

    log.debug("{} alerts saved", alerts.size());
    return alerts;
  }

  @Transactional
  public Alert save(Alert alert) {
    return save(Collections.singletonList(alert)).get(0);
  }

  public Alert get(UUID uuid) {
    if (uuid == null) {
      throw new IllegalArgumentException("Can't get alert by null uuid");
    }
    return list(AlertFilter.builder().uuid(uuid).build()).stream().findFirst().orElse(null);
  }

  public Alert getOrBadRequest(UUID uuid) {
    if (uuid == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert UUID: " + uuid);
    }
    Alert alert = get(uuid);
    if (alert == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert UUID: " + uuid);
    }
    return alert;
  }

  @Transactional
  public List<Alert> markResolved(AlertFilter filter) {
    AlertFilter notResolved =
        filter.toBuilder().targetState(Alert.State.CREATED, Alert.State.ACTIVE).build();
    List<Alert> resolved =
        list(notResolved)
            .stream()
            .map(alert -> alert.setTargetState(Alert.State.RESOLVED))
            .collect(Collectors.toList());
    return save(resolved);
  }

  public List<Alert> list(AlertFilter filter) {
    return createQueryByFilter(filter).findList();
  }

  public List<Alert> listNotResolved(AlertFilter filter) {
    AlertFilter notResolved = filter.toBuilder().targetState(Alert.State.ACTIVE).build();
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
    log.debug("Alert {} deleted", uuid);
  }

  @Transactional
  public void delete(AlertFilter filter) {
    int deleted = createQueryByFilter(filter).delete();
    log.debug("{} alerts deleted", deleted);
  }

  /**
   * Required to make alert labels consistent between definition based alerts and manual alerts.
   * Will only need to insert definition related labels once all alerts will have underlying
   * definition.
   */
  private Alert prepareForSave(Alert alert) {
    return alert
        .setLabel(KnownAlertLabels.CUSTOMER_UUID, alert.getCustomerUUID().toString())
        .setLabel(KnownAlertLabels.ERROR_CODE, alert.getErrCode())
        .setLabel(KnownAlertLabels.ALERT_TYPE, alert.getType());
  }

  private void validate(Alert alert) {
    if (alert.getCustomerUUID() == null) {
      throw new IllegalArgumentException("Customer UUID field is mandatory");
    }
    if (StringUtils.isEmpty(alert.getType())) {
      throw new IllegalArgumentException("Alert type field is mandatory");
    }
    if (StringUtils.isEmpty(alert.getErrCode())) {
      throw new IllegalArgumentException("Error code field is mandatory");
    }
    if (StringUtils.isEmpty(alert.getMessage())) {
      throw new IllegalArgumentException("Message field is mandatory");
    }
  }
}
