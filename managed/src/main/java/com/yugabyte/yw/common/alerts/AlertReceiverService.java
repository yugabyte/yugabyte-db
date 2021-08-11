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

import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import io.ebean.ExpressionList;
import io.ebean.annotation.Transactional;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.data.validation.Constraints.Required;

@Singleton
@Slf4j
public class AlertReceiverService {

  public AlertReceiver get(UUID customerUUID, UUID receiverUUID) {
    return AlertReceiver.get(customerUUID, receiverUUID);
  }

  private AlertReceiver get(UUID customerUUID, String receiverName) {
    return AlertReceiver.createQuery()
        .eq("customerUUID", customerUUID)
        .eq("name", receiverName)
        .findOne();
  }

  public AlertReceiver getOrBadRequest(UUID customerUUID, @Required UUID receiverUUID) {
    AlertReceiver alertReceiver = get(customerUUID, receiverUUID);
    if (alertReceiver == null) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Receiver UUID: " + receiverUUID);
    }
    return alertReceiver;
  }

  public List<AlertReceiver> getOrBadRequest(UUID customerUUID, @Required List<UUID> uuids) {
    ExpressionList<AlertReceiver> query =
        AlertReceiver.createQuery().eq("customerUUID", customerUUID);
    appendInClause(query, "uuid", uuids);
    List<AlertReceiver> result = query.findList();
    if (result.size() != uuids.size()) {
      // We have incorrect receiver id(s).
      List<UUID> uuidsToRemove =
          result.stream().map(AlertReceiver::getUuid).collect(Collectors.toList());
      List<UUID> uuidsToReport = new ArrayList<>(uuids);
      uuidsToReport.removeAll(uuidsToRemove);
      throw new YWServiceException(
          BAD_REQUEST,
          "Invalid Alert Receiver UUID: "
              + uuidsToReport
                  .stream()
                  .map(uuid -> uuid.toString())
                  .collect(Collectors.joining(", ")));
    }
    return result;
  }

  public List<AlertReceiver> list(UUID customerUUID) {
    return AlertReceiver.createQuery().eq("customerUUID", customerUUID).findList();
  }

  @Transactional
  public AlertReceiver save(AlertReceiver receiver) {
    if (receiver.getUuid() == null) {
      receiver.generateUUID();
    }

    AlertReceiver valueWithSameName = get(receiver.getCustomerUUID(), receiver.getName());
    if ((valueWithSameName != null) && !receiver.getUuid().equals(valueWithSameName.getUuid())) {
      throw new YWServiceException(BAD_REQUEST, "Alert receiver with such name already exists.");
    }

    try {
      validate(receiver);
    } catch (YWValidateException e) {
      throw new YWServiceException(
          BAD_REQUEST, "Unable to create/update alert receiver: " + e.getMessage());
    }

    receiver.save();
    return receiver;
  }

  public void delete(UUID customerUUID, UUID receiverUUID) {
    AlertReceiver receiver = getOrBadRequest(customerUUID, receiverUUID);

    List<String> blockingRoutes =
        receiver
            .getRoutesList()
            .stream()
            .filter(route -> route.getReceiversList().size() == 1)
            .map(AlertRoute::getName)
            .sorted()
            .collect(Collectors.toList());
    if (!blockingRoutes.isEmpty()) {
      throw new YWServiceException(
          BAD_REQUEST,
          String.format(
              "Unable to delete alert receiver: %s. %d alert routes have it as a last receiver."
                  + " Examples: %s",
              receiverUUID,
              blockingRoutes.size(),
              blockingRoutes.stream().limit(5).collect(Collectors.toList())));
    }

    if (!receiver.delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert receiver: " + receiverUUID);
    }
    log.info("Deleted alert receiver {} for customer {}", receiverUUID, customerUUID);
  }

  public void validate(AlertReceiver receiver) throws YWValidateException {
    if (receiver.getParams() == null) {
      throw new YWValidateException("Incorrect parameters in AlertReceiver.");
    }
    receiver.getParams().validate();
  }
}
