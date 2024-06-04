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

import static com.yugabyte.yw.models.MaintenanceWindow.createQueryByFilter;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static com.yugabyte.yw.models.helpers.CommonUtils.performPagedQuery;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.MaintenanceWindow;
import com.yugabyte.yw.models.MaintenanceWindow.SortBy;
import com.yugabyte.yw.models.filters.MaintenanceWindowFilter;
import com.yugabyte.yw.models.helpers.EntityOperation;
import com.yugabyte.yw.models.paging.MaintenanceWindowPagedQuery;
import com.yugabyte.yw.models.paging.MaintenanceWindowPagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import io.ebean.DB;
import io.ebean.Query;
import io.ebean.annotation.Transactional;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Singleton
@Slf4j
public class MaintenanceService {

  private final BeanValidator beanValidator;

  @Inject
  public MaintenanceService(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
  }

  @Transactional
  public List<MaintenanceWindow> save(List<MaintenanceWindow> maintenanceWindows) {
    if (CollectionUtils.isEmpty(maintenanceWindows)) {
      return maintenanceWindows;
    }

    List<MaintenanceWindow> beforeWindows = Collections.emptyList();
    Set<UUID> windowUuids =
        maintenanceWindows.stream()
            .filter(alert -> !alert.isNew())
            .map(MaintenanceWindow::getUuid)
            .collect(Collectors.toSet());
    if (!windowUuids.isEmpty()) {
      MaintenanceWindowFilter filter = MaintenanceWindowFilter.builder().uuids(windowUuids).build();
      beforeWindows = list(filter);
    }
    Map<UUID, MaintenanceWindow> beforeWindowsMap =
        beforeWindows.stream()
            .collect(Collectors.toMap(MaintenanceWindow::getUuid, Function.identity()));

    Map<EntityOperation, List<MaintenanceWindow>> toCreateAndUpdate =
        maintenanceWindows.stream()
            .map(window -> prepareForSave(window, beforeWindowsMap.get(window.getUuid())))
            .filter(window -> filterForSave(window, beforeWindowsMap.get(window.getUuid())))
            .peek(window -> validate(window, beforeWindowsMap.get(window.getUuid())))
            .collect(Collectors.groupingBy(window -> window.isNew() ? CREATE : UPDATE));

    List<MaintenanceWindow> toCreate =
        toCreateAndUpdate.getOrDefault(CREATE, Collections.emptyList());
    if (!toCreate.isEmpty()) {
      toCreate.forEach(MaintenanceWindow::generateUUID);
      DB.getDefault().saveAll(toCreate);
    }

    List<MaintenanceWindow> toUpdate =
        toCreateAndUpdate.getOrDefault(UPDATE, Collections.emptyList());
    if (!toUpdate.isEmpty()) {
      DB.getDefault().updateAll(toUpdate);
    }

    log.trace("{} maintenance windows saved", toCreate.size() + toUpdate.size());
    return maintenanceWindows;
  }

  @Transactional
  public MaintenanceWindow save(MaintenanceWindow maintenanceWindow) {
    return save(Collections.singletonList(maintenanceWindow)).get(0);
  }

  public MaintenanceWindow get(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't get maintenance window by null uuid");
    }
    return list(MaintenanceWindowFilter.builder().uuid(uuid).build()).stream()
        .findFirst()
        .orElse(null);
  }

  public MaintenanceWindow getOrBadRequest(UUID uuid) {
    if (uuid == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Maintenance Window UUID: " + uuid);
    }
    MaintenanceWindow window = get(uuid);
    if (window == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Maintenance Window UUID: " + uuid);
    }
    return window;
  }

  public MaintenanceWindow getOrBadRequest(UUID customerUUID, UUID uuid) {
    MaintenanceWindow maintenanceWindow = getOrBadRequest(uuid);
    if (!maintenanceWindow.getCustomerUUID().equals(customerUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Maintenance Window UUID: " + uuid);
    }
    return maintenanceWindow;
  }

  public List<MaintenanceWindow> list(MaintenanceWindowFilter filter) {
    return createQueryByFilter(filter).orderBy().desc("createTime").findList();
  }

  public int count(MaintenanceWindowFilter filter) {
    return createQueryByFilter(filter).findCount();
  }

  public MaintenanceWindowPagedResponse pagedList(MaintenanceWindowPagedQuery pagedQuery) {
    if (pagedQuery.getSortBy() == null) {
      pagedQuery.setSortBy(SortBy.createTime);
      pagedQuery.setDirection(SortDirection.DESC);
    }
    Query<MaintenanceWindow> query =
        MaintenanceWindow.createQueryByFilter(pagedQuery.getFilter()).query();
    return performPagedQuery(query, pagedQuery, MaintenanceWindowPagedResponse.class);
  }

  @Transactional
  public void delete(UUID uuid) {
    MaintenanceWindow window = get(uuid);
    if (window == null) {
      log.warn("Maintenance window {} is already deleted", uuid);
      return;
    }
    window.delete();
    log.trace("Maintenance window {} deleted", uuid);
  }

  @Transactional
  public int delete(MaintenanceWindowFilter filter) {
    int deleted = createQueryByFilter(filter).delete();
    log.trace("{} maintenance windows deleted", deleted);
    return deleted;
  }

  private MaintenanceWindow prepareForSave(MaintenanceWindow window, MaintenanceWindow before) {
    if (before != null) {
      window.setCreateTime(before.getCreateTime());
      if (!before.getAlertConfigurationFilter().equals(window.getAlertConfigurationFilter())) {
        // In case filter changed - need to reapply this window on existing alert configs
        window.setAppliedToAlertConfigurations(false);
      }
    } else {
      window.setCreateTime(nowWithoutMillis());
    }
    return window;
  }

  private boolean filterForSave(MaintenanceWindow window, MaintenanceWindow before) {
    if (window == null) {
      return true;
    }
    return !window.equals(before);
  }

  private void validate(MaintenanceWindow window, MaintenanceWindow before) {
    beanValidator.validate(window);
    if (!window.getStartTime().before(window.getEndTime())) {
      beanValidator.error().forField("endTime", "should be after startTime").throwError();
    }
    if (before != null) {
      if (!window.getCustomerUUID().equals(before.getCustomerUUID())) {
        beanValidator
            .error()
            .forField(
                "customerUUID", "can't change for maintenance window '" + window.getUuid() + "'")
            .throwError();
      }
      if (!window.getCreateTime().equals(before.getCreateTime())) {
        beanValidator
            .error()
            .forField(
                "createTime", "can't change for maintenance window '" + window.getUuid() + "'")
            .throwError();
      }
    } else if (!window.isNew()) {
      beanValidator
          .error()
          .forField("", "can't update missing maintenance window '" + window.getUuid() + "'")
          .throwError();
    }
  }
}
