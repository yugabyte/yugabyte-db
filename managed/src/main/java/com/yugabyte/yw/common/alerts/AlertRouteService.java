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

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import io.ebean.annotation.Transactional;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class AlertRouteService {

  private final AlertDefinitionGroupService alertDefinitionGroupService;

  private final AlertReceiverService alertReceiverService;

  @Inject
  public AlertRouteService(
      AlertReceiverService alertReceiverService,
      AlertDefinitionGroupService alertDefinitionGroupService) {
    this.alertReceiverService = alertReceiverService;
    this.alertDefinitionGroupService = alertDefinitionGroupService;
  }

  public void delete(UUID customerUUID, UUID routeUUID) {
    AlertRoute route = getOrBadRequest(customerUUID, routeUUID);
    if (route.isDefaultRoute()) {
      throw new YWServiceException(
          BAD_REQUEST,
          String.format(
              "Unable to delete default alert route %s, make another route default at first.",
              routeUUID));
    }

    AlertDefinitionGroupFilter groupFilter =
        AlertDefinitionGroupFilter.builder().routeUuid(route.getUuid()).build();
    List<AlertDefinitionGroup> groups = alertDefinitionGroupService.list(groupFilter);
    if (!groups.isEmpty()) {
      throw new YWServiceException(
          BAD_REQUEST,
          "Unable to delete alert route: "
              + routeUUID
              + ". "
              + groups.size()
              + " alert definition groups are linked to it. Examples: "
              + groups
                  .stream()
                  .limit(5)
                  .map(AlertDefinitionGroup::getName)
                  .collect(Collectors.toList()));
    }
    if (!route.delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert route: " + routeUUID);
    }
    log.info("Deleted alert route {} for customer {}", routeUUID, customerUUID);
  }

  @Transactional
  public AlertRoute save(AlertRoute route) {
    AlertRoute oldValue = null;
    if (route.getUuid() == null) {
      route.generateUUID();
    } else {
      oldValue = get(route.getCustomerUUID(), route.getUuid());
    }

    try {
      validate(oldValue, route);
    } catch (YWValidateException e) {
      throw new YWServiceException(
          BAD_REQUEST, "Unable to create/update alert route: " + e.getMessage());
    }

    // Next will check that all the receivers exist.
    alertReceiverService.getOrBadRequest(
        route.getCustomerUUID(),
        route.getReceiversList().stream().map(AlertReceiver::getUuid).collect(Collectors.toList()));

    AlertRoute defaultRoute = getDefaultRoute(route.getCustomerUUID());
    route.save();

    // Resetting default route flag for the previous default route only if the
    // new route save succeeded.
    if (route.isDefaultRoute()
        && (defaultRoute != null)
        && !defaultRoute.getUuid().equals(route.getUuid())) {
      defaultRoute.setDefaultRoute(false);
      defaultRoute.save();
      log.info(
          "For customer {} switched default route to {}", route.getCustomerUUID(), route.getUuid());
    }
    return route;
  }

  private AlertRoute get(UUID customerUUID, String routeName) {
    return AlertRoute.createQuery()
        .eq("customerUUID", customerUUID)
        .eq("name", routeName)
        .findOne();
  }

  public AlertRoute get(UUID customerUUID, UUID routeUUID) {
    return AlertRoute.get(customerUUID, routeUUID);
  }

  public AlertRoute getOrBadRequest(UUID customerUUID, UUID routeUUID) {
    AlertRoute alertRoute = get(customerUUID, routeUUID);
    if (alertRoute == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Route UUID: " + routeUUID);
    }
    return alertRoute;
  }

  public List<AlertRoute> listByCustomer(UUID customerUUID) {
    return AlertRoute.createQuery().eq("customerUUID", customerUUID).findList();
  }

  public AlertRoute getDefaultRoute(UUID customerUUID) {
    return AlertRoute.createQuery()
        .eq("customerUUID", customerUUID)
        .eq("defaultRoute", true)
        .findOne();
  }

  /**
   * Creates default route for the specified customer. Created route has only one receiver of type
   * Email with the set of passed recipients. Also it doesn't have own SMTP configuration and uses
   * default SMTP settings (from the platform configuration).
   *
   * @param customerUUID
   * @param recipients
   * @return
   */
  public AlertRoute createDefaultRoute(UUID customerUUID) {
    AlertReceiverEmailParams defaultParams = new AlertReceiverEmailParams();
    defaultParams.defaultSmtpSettings = true;
    defaultParams.defaultRecipients = true;
    AlertReceiver defaultReceiver =
        new AlertReceiver()
            .setCustomerUUID(customerUUID)
            .setName("Default Receiver")
            .setParams(defaultParams);
    defaultReceiver = alertReceiverService.save(defaultReceiver);

    AlertRoute route =
        new AlertRoute()
            .setCustomerUUID(customerUUID)
            .setName("Default Route")
            .setReceiversList(Collections.singletonList(defaultReceiver))
            .setDefaultRoute(true);
    save(route);
    return route;
  }

  private void validate(AlertRoute oldValue, AlertRoute route) throws YWValidateException {
    if (CollectionUtils.isEmpty(route.getReceiversList())) {
      throw new YWValidateException("Can't save alert route without receivers.");
    }

    if (StringUtils.isEmpty(route.getName())) {
      throw new YWValidateException("Name is mandatory.");
    }

    if (route.getName().length() > AlertRoute.MAX_NAME_LENGTH / 4) {
      throw new YWValidateException(
          String.format("Name length (%d) is exceeded.", AlertReceiver.MAX_NAME_LENGTH / 4));
    }

    if ((oldValue != null) && oldValue.isDefaultRoute() && !route.isDefaultRoute()) {
      throw new YWValidateException(
          "Can't set the alert route as non-default. Make another route as default at first.");
    }

    AlertRoute valueWithSameName = get(route.getCustomerUUID(), route.getName());
    if ((valueWithSameName != null) && !route.getUuid().equals(valueWithSameName.getUuid())) {
      throw new YWValidateException("Alert route with such name already exists.");
    }
  }
}
