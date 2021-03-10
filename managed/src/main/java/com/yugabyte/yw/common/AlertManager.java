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

import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.models.*;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.net.InetAddress;
import java.net.UnknownHostException;
import javax.inject.Inject;
import javax.inject.Singleton;
import javax.mail.MessagingException;

import java.util.Collections;
import java.util.List;

@Singleton
public class AlertManager {

  @Inject
  private EmailHelper emailHelper;

  public static final Logger LOG = LoggerFactory.getLogger(AlertManager.class);
  // @formatter:off
  private static final String STYLE_FONT =
    "font-family: SF Pro Display, SF Pro, Helvetica Neue, Helvetica, sans-serif;";

  /**
   * Sends email notification with information about the alert. Doesn't send email
   * if:<br>
   * <ul>
   * <li>The alert has no flag {@link Alert#sendEmail} set;</li>
   * <li>Destinations list (with recipients) for this customer is empty;</li>
   * <li>SmtpData for this customer is empty/incorrect
   * {@link CustomerRegisterFormData.SmtpData};</li>
   * <li>The alert is related to a deleted universe.</li>
   * </ul>
   *
   * @param alert  The alert to be processed
   * @param state  The new state of the alert
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

    AlertDefinition definition = alert.definitionUUID == null ? null
        : AlertDefinition.get(alert.definitionUUID);
    String content;
    Universe universe;
    String subject;
    if (definition != null) {
      // The universe should exist (otherwise the definition should not exist as
      // well).
      universe = Universe.get(definition.universeUUID);
      content = String.format("%s for %s is %s.", definition.name /* alert_name */, universe.name,
          state);
    } else {
      universe = alert.targetType == Alert.TargetType.UniverseType
          ? Universe.find.byId(alert.targetUUID)
          : null;
      if (universe != null) {
        content = String.format(
            "Common failure for universe '%s', state: %s\nFailure details:\n\n%s.",
            universe.name, state, alert.message);
      } else {
        content = String.format(
            "Common failure for customer '%s', state: %s\nFailure details:\n\n%s.",
            customer.name, state, alert.message);
      }
    }
    subject = String.format("Yugabyte Platform Alert ERROR- <%s> %s", customer.getTag(),
      universe.name);
    String style = String
      .format("%s font-size: 14px;background-color:#f7f7f7;padding:25px 30px 5px;", STYLE_FONT);
    String hostname = "";
    String ip = "";
    try {
      hostname = InetAddress.getLocalHost().getHostName();
      ip = InetAddress.getLocalHost().getHostAddress().toString();
    } catch (UnknownHostException e) {
      LOG.error("Could not determine the hostname", e);
    }

    String header = String.format(
      "<table width=\"100%%\">\n" +
        "    <tr>\n" +
        "        <td style=\"text-align:left\">%s</td>\n" +
        "        <td style=\"text-align:left\">%s</td>\n" +
        "    </tr>\n" +
        "    <tr>\n" +
        "        <td style=\"text-align:left\">%s</td>\n" +
        "        <td style=\"text-align:left\">%s</td>\n" +
        "    </tr>\n" +
        "</table>\n",
      makeHeaderLeft("Universe name", universe.name),
      makeHeaderLeft("Universe version", (universe.version+"")),
      makeHeaderLeft("YW host name", hostname),
      makeHeaderLeft("YW host IP", ip));

    content = String.format("<html><body><pre style=\"%s\">%s\n%s</pre></body></html>", style,
      header, content.toString());

    try {
      emailHelper.sendEmail(customer, subject, String.join(",", destinations), smtpData,
          Collections.singletonMap("text/html; charset=\"us-ascii\"", content));
    } catch (MessagingException e) {
      LOG.error("Error sending email for alert {} in state '{}'", alert.uuid, state, e);
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

  private static String makeHeaderLeft(String title, String content) {
    return String.format(
      "%s<h1 style=\"%sline-height:1em;color:#202951;font-weight:700;font-size:1.85em;\n" +
        "padding-bottom:15px;margin:0;\">%s</h1>",
      makeSubtitle(title), STYLE_FONT, content);
  }

  private static String makeSubtitle(String content) {
    return String.format("<span style=\"color:#8D8F9D;font-weight:400;\">%s:</span>\n", content);
  }
}
