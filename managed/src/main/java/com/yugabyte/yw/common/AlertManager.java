/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.Alert.State;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import javax.inject.Inject;
import javax.inject.Singleton;
import javax.mail.MessagingException;

import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

@Singleton
public class AlertManager {

  @VisibleForTesting static final String ALERT_MANAGER_ERROR_CODE = "ALERT_MANAGER_FAILURE";

  @Inject private EmailHelper emailHelper;

  @Inject private AlertDefinitionService alertDefinitionService;

  public static final Logger LOG = LoggerFactory.getLogger(AlertManager.class);

  /**
   * Sends email notification with information about the alert. Doesn't send email if:<br>
   *
   * <ul>
   *   <li>The alert has no flag {@link Alert#sendEmail} set;
   *   <li>Destinations list (with recipients) for this customer is empty;
   *   <li>SmtpData for this customer is empty/incorrect {@link CustomerRegisterFormData.SmtpData};
   *   <li>The alert is related to a deleted universe.
   * </ul>
   *
   * @param alert The alert to be processed
   * @param state The new state of the alert
   */
  public void sendEmail(Alert alert, String state) {
    LOG.debug("sendEmail {}, state: {}", alert, state);
    if (!alert.sendEmail) {
      return;
    }

    Customer customer = Customer.get(alert.customerUUID);
    List<String> destinations = emailHelper.getDestinations(customer.uuid);
    // Skip sending email if there aren't any destinations to send it to.
    if (destinations.isEmpty()) {
      return;
    }

    CustomerRegisterFormData.SmtpData smtpData = emailHelper.getSmtpData(customer.uuid);
    // Skip if the SMTP configuration is not completely defined.
    if (smtpData == null) {
      return;
    }

    String subject = String.format("Yugabyte Platform Alert - <%s>", customer.getTag());
    AlertDefinition definition =
        alert.definitionUUID == null ? null : alertDefinitionService.get(alert.definitionUUID);
    String content;
    if (definition != null) {
      // The universe should exist (otherwise the definition should not exist as
      // well).
      // TODO notification templates should be reimplemented to base on alert message
      // For now this code only supports definitions with Universe target type.
      Universe universe = Universe.getOrBadRequest(definition.getUniverseUUID());
      content = String.format("%s for %s is %s.", definition.getName(), universe.name, state);
    } else {
      Universe universe =
          alert.targetType == Alert.TargetType.UniverseType
              ? Universe.find.byId(alert.targetUUID)
              : null;
      if (universe != null) {
        content =
            String.format(
                "Common failure for universe '%s', state: %s\nFailure details:\n\n%s",
                universe.name, state, alert.message);
      } else {
        content =
            String.format(
                "Common failure for customer '%s', state: %s\nFailure details:\n\n%s",
                customer.name, state, alert.message);
      }
    }

    try {
      emailHelper.sendEmail(
          customer,
          subject,
          String.join(",", destinations),
          smtpData,
          Collections.singletonMap("text/plain; charset=\"us-ascii\"", content));
      resolveAlerts(customer.uuid, smtpData.configUUID, ALERT_MANAGER_ERROR_CODE);
    } catch (MessagingException e) {
      String error =
          String.format(
              "Error sending email for alert %s in state '%s': %s",
              alert.uuid, state, e.getMessage());
      LOG.error(error);
      createAlert(customer, smtpData.configUUID, error);
    }
  }

  /**
   * A method to run a state transition for a given alert
   *
   * @param alert the alert to transition states on
   * @return the alert in a new state
   */
  public Alert transitionAlert(Alert alert) {
    try {
      switch (alert.state) {
        case CREATED:
          LOG.info("Transitioning alert {} to active", alert.uuid);
          sendEmail(alert, "FIRING");
          alert.setState(Alert.State.ACTIVE);
          break;
        case ACTIVE:
          LOG.info("Transitioning alert {} to resolved (with email)", alert.uuid);
          sendEmail(alert, "RESOLVED");
          alert.setState(Alert.State.RESOLVED);
          break;
        case RESOLVED:
          LOG.info("Transitioning alert {} to resolved (no email)", alert.uuid);
          alert.setState(Alert.State.RESOLVED);
          break;
      }

      alert.save();
    } catch (Exception e) {
      LOG.error("Error transitioning alert state for alert {}", alert.uuid, e);
    }

    return alert;
  }

  /**
   * Updates states of all active alerts (according to criteria) to RESOLVED.
   *
   * @param customerUUID
   * @param targetUUID
   * @param errorCode Error code string (LIKE wildcards allowed)
   */
  public void resolveAlerts(UUID customerUUID, UUID targetUUID, String errorCode) {
    List<Alert> activeAlerts =
        Alert.list(customerUUID, errorCode, targetUUID)
            .stream()
            .filter(alert -> alert.state == State.ACTIVE || alert.state == State.CREATED)
            .collect(Collectors.toList());
    LOG.debug("Resetting alerts for '{}', count {}", errorCode, activeAlerts.size());
    for (Alert alert : activeAlerts) {
      alert.setState(State.RESOLVED);
      alert.save();
    }
  }

  private void createAlert(Customer c, UUID configUUID, String details) {
    if (Alert.getActiveCustomerAlertsByTargetUuid(c.uuid, configUUID).size() == 0) {
      Alert.create(
          c.uuid,
          configUUID,
          Alert.TargetType.CustomerConfigType,
          ALERT_MANAGER_ERROR_CODE,
          "Error",
          details);
    }
  }
}
