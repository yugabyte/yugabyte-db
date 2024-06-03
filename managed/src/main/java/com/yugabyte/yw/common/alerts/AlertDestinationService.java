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

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDestination;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import io.ebean.annotation.Transactional;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class AlertDestinationService {

  private final BeanValidator beanValidator;

  private final AlertConfigurationService alertConfigurationService;

  private final AlertChannelService alertChannelService;

  @Inject
  public AlertDestinationService(
      BeanValidator beanValidator,
      AlertChannelService alertChannelService,
      AlertConfigurationService alertConfigurationService) {
    this.beanValidator = beanValidator;
    this.alertChannelService = alertChannelService;
    this.alertConfigurationService = alertConfigurationService;
  }

  public void delete(UUID customerUUID, UUID destinationUUID) {
    AlertDestination destination = getOrBadRequest(customerUUID, destinationUUID);
    if (destination.isDefaultDestination()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Unable to delete default alert destination '%s',"
                  + " make another destination default at first.",
              destination.getName()));
    }

    AlertConfigurationFilter configurationFilter =
        AlertConfigurationFilter.builder().destinationUuid(destination.getUuid()).build();
    List<AlertConfiguration> configurations = alertConfigurationService.list(configurationFilter);
    if (!configurations.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Unable to delete alert destination '"
              + destination.getName()
              + "'. "
              + configurations.size()
              + " alert configurations are linked to it. Examples: "
              + configurations.stream()
                  .limit(5)
                  .map(AlertConfiguration::getName)
                  .collect(Collectors.toList()));
    }
    if (!destination.delete()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert destination: " + destination.getName());
    }
    log.info("Deleted alert destination {} for customer {}", destinationUUID, customerUUID);
  }

  @Transactional
  public AlertDestination save(AlertDestination destination) {
    AlertDestination oldValue = null;
    if (destination.getUuid() == null) {
      destination.generateUUID();
    } else {
      oldValue = get(destination.getCustomerUUID(), destination.getUuid());
    }

    validate(oldValue, destination);

    // Next will check that all the channels exist.
    alertChannelService.getOrBadRequest(
        destination.getCustomerUUID(),
        destination.getChannelsList().stream()
            .map(AlertChannel::getUuid)
            .collect(Collectors.toList()));

    AlertDestination defaultDestination = getDefaultDestination(destination.getCustomerUUID());
    destination.save();

    // Resetting default destination flag for the previous default destination only if the
    // new destination save succeeded.
    if (destination.isDefaultDestination()
        && (defaultDestination != null)
        && !defaultDestination.getUuid().equals(destination.getUuid())) {
      defaultDestination.setDefaultDestination(false);
      defaultDestination.save();
      log.info(
          "For customer {} switched default destination to {}",
          destination.getCustomerUUID(),
          destination.getUuid());
    }
    return destination;
  }

  private AlertDestination get(UUID customerUUID, String destinationName) {
    return AlertDestination.createQuery()
        .eq("customerUUID", customerUUID)
        .eq("name", destinationName)
        .findOne();
  }

  public AlertDestination get(UUID customerUUID, UUID destinationUUID) {
    return AlertDestination.get(customerUUID, destinationUUID);
  }

  public AlertDestination getOrBadRequest(UUID customerUUID, UUID destinationUUID) {
    AlertDestination alertDestination = get(customerUUID, destinationUUID);
    if (alertDestination == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid Alert Destination UUID: " + destinationUUID);
    }
    return alertDestination;
  }

  public List<AlertDestination> listByCustomer(UUID customerUUID) {
    return AlertDestination.createQuery().eq("customerUUID", customerUUID).findList();
  }

  public AlertDestination getDefaultDestination(UUID customerUUID) {
    return AlertDestination.createQuery()
        .eq("customerUUID", customerUUID)
        .eq("defaultDestination", true)
        .findOne();
  }

  /**
   * Creates default destination for the specified customer. Created destination has only one
   * channel of type Email with the set of passed recipients. Also it doesn't have own SMTP
   * configuration and uses default SMTP settings (from the platform configuration).
   *
   * @param customerUUID
   * @return
   */
  public AlertDestination createDefaultDestination(UUID customerUUID) {
    AlertChannelEmailParams defaultParams = new AlertChannelEmailParams();
    defaultParams.setDefaultSmtpSettings(true);
    defaultParams.setDefaultRecipients(true);
    AlertChannel defaultChannel =
        new AlertChannel()
            .setCustomerUUID(customerUUID)
            .setName("Default Channel")
            .setParams(defaultParams);
    defaultChannel = alertChannelService.save(defaultChannel);

    AlertDestination destination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName("Default Destination")
            .setChannelsList(Collections.singletonList(defaultChannel))
            .setDefaultDestination(true);
    save(destination);
    return destination;
  }

  private void validate(AlertDestination oldValue, AlertDestination destination) {
    beanValidator.validate(destination);

    if ((oldValue != null)
        && oldValue.isDefaultDestination()
        && !destination.isDefaultDestination()) {
      beanValidator
          .error()
          .forField(
              "defaultDestination",
              "can't set the alert destination as non-default -"
                  + " make another destination as default at first.")
          .throwError();
    }

    AlertDestination valueWithSameName = get(destination.getCustomerUUID(), destination.getName());
    if ((valueWithSameName != null) && !destination.getUuid().equals(valueWithSameName.getUuid())) {
      beanValidator
          .error()
          .forField("name", "alert destination with such name already exists.")
          .throwError();
    }
  }
}
