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

import static com.yugabyte.yw.models.helpers.CommonUtils.appendInClause;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertDestination;
import io.ebean.ExpressionList;
import io.ebean.annotation.Transactional;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.data.validation.Constraints.Required;

@Singleton
@Slf4j
public class AlertChannelService {

  private final BeanValidator beanValidator;

  @Inject
  public AlertChannelService(BeanValidator beanValidator) {
    this.beanValidator = beanValidator;
  }

  public AlertChannel get(UUID customerUUID, UUID channelUUID) {
    return AlertChannel.get(customerUUID, channelUUID);
  }

  private AlertChannel get(UUID customerUUID, String channelName) {
    return AlertChannel.createQuery()
        .eq("customerUUID", customerUUID)
        .eq("name", channelName)
        .findOne();
  }

  public AlertChannel getOrBadRequest(UUID customerUUID, @Required UUID channelName) {
    AlertChannel alertChannel = get(customerUUID, channelName);
    if (alertChannel == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid Alert Channel UUID: " + channelName);
    }
    return alertChannel;
  }

  public List<AlertChannel> getOrBadRequest(UUID customerUUID, @Required List<UUID> uuids) {
    ExpressionList<AlertChannel> query =
        AlertChannel.createQuery().eq("customerUUID", customerUUID);
    appendInClause(query, "uuid", uuids);
    List<AlertChannel> result = query.findList();
    if (result.size() != uuids.size()) {
      // We have incorrect channel id(s).
      Set<UUID> uuidsToRemove =
          result.stream().map(AlertChannel::getUuid).collect(Collectors.toSet());
      Set<UUID> uuidsToReport = new HashSet<>(uuids);
      uuidsToReport.removeAll(uuidsToRemove);
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Invalid Alert Channel UUID: "
              + uuidsToReport.stream()
                  .map(uuid -> uuid.toString())
                  .collect(Collectors.joining(", ")));
    }
    return result;
  }

  public List<AlertChannel> list(UUID customerUUID) {
    return AlertChannel.createQuery().eq("customerUUID", customerUUID).findList();
  }

  @Transactional
  public AlertChannel save(AlertChannel channel) {
    if (channel.getUuid() == null) {
      channel.generateUUID();
    }

    validate(channel);

    channel.save();
    return channel;
  }

  public void delete(UUID customerUUID, UUID channelUUID) {
    AlertChannel channel = getOrBadRequest(customerUUID, channelUUID);

    List<String> blockingDestinations =
        channel.getDestinationsList().stream()
            .filter(destination -> destination.getChannelsList().size() == 1)
            .map(AlertDestination::getName)
            .sorted()
            .collect(Collectors.toList());
    if (!blockingDestinations.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Unable to delete alert channel: %s. %d alert destinations have it as a last channel."
                  + " Examples: %s",
              channelUUID,
              blockingDestinations.size(),
              blockingDestinations.stream().limit(5).collect(Collectors.toList())));
    }

    if (!channel.delete()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert channel: " + channelUUID);
    }
    log.info("Deleted alert channel {} for customer {}", channelUUID, customerUUID);
  }

  public void validate(AlertChannel channel) {
    beanValidator.validate(channel);

    AlertChannel valueWithSameName = get(channel.getCustomerUUID(), channel.getName());
    if ((valueWithSameName != null) && !channel.getUuid().equals(valueWithSameName.getUuid())) {
      beanValidator
          .error()
          .forField("name", "alert channel with such name already exists.")
          .throwError();
    }

    channel.getParams().validate(beanValidator);
  }
}
