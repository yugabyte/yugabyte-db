/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.alerts.impl;

import static com.yugabyte.yw.common.alerts.impl.AlertChannelBase.DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE;
import static com.yugabyte.yw.common.alerts.impl.AlertChannelBase.DEFAULT_ALERT_NOTIFICATION_TITLE_TEMPLATE;
import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertTemplateSubstitutor;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;

public class AlertChannelBaseTest extends FakeDBApplication {

  private static final String TITLE_TEMPLATE = "<b>Title template</b>";

  private static final String TEXT_TEMPLATE = "<html>Text template is here</html>";

  private static final String ALERT_CHANNEL_NAME = "Test AlertChannel";

  private Customer defaultCustomer;
  AlertChannelBase channelBase;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    channelBase =
        new AlertChannelBase() {
          @Override
          public void sendNotification(Customer customer, Alert alert, AlertChannel channel)
              throws PlatformNotificationException {
            // Do nothing
          }
        };
  }

  @Test
  public void testGetNotificationTitle_TemplateInChannel() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    alert.setDefinitionUuid(definition.getUuid());
    AlertChannel channel = createEmailChannel();

    assertEquals(
        channel.getParams().getTitleTemplate(), channelBase.getNotificationTitle(alert, channel));
  }

  @Test
  public void testGetNotificationTitle_DefaultTitle() {
    Alert alert = ModelFactory.createAlert(defaultCustomer);
    AlertChannel channel = createEmailChannelWithEmptyTemplates();

    AlertTemplateSubstitutor<Alert> substitutor = new AlertTemplateSubstitutor<>(alert);
    assertEquals(
        substitutor.replace(DEFAULT_ALERT_NOTIFICATION_TITLE_TEMPLATE),
        channelBase.getNotificationTitle(alert, channel));
  }

  @Test
  public void testGetNotificationText_TemplateInChannel() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    AlertChannel channel = createEmailChannel();
    assertEquals(
        channel.getParams().getTextTemplate(), channelBase.getNotificationText(alert, channel));
  }

  @Test
  public void testGetNotificationText_TemplateInAlert() {
    Universe universe = ModelFactory.createUniverse();
    AlertConfiguration configuration =
        ModelFactory.createAlertConfiguration(defaultCustomer, universe);
    AlertDefinition definition =
        ModelFactory.createAlertDefinition(defaultCustomer, universe, configuration);

    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);
    alert.setDefinitionUuid(definition.getUuid());

    List<AlertLabel> labels =
        definition
            .getEffectiveLabels(configuration, null, AlertConfiguration.Severity.SEVERE)
            .stream()
            .map(l -> new AlertLabel(l.getName(), l.getValue()))
            .collect(Collectors.toList());
    alert.setLabels(labels);
    AlertChannel channel = createEmailChannelWithEmptyTemplates();

    AlertTemplateSubstitutor<Alert> substitutor = new AlertTemplateSubstitutor<>(alert);
    assertEquals(
        substitutor.replace(DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE),
        channelBase.getNotificationText(alert, channel));
  }

  private AlertChannel createEmailChannel() {
    AlertChannel channel =
        ModelFactory.createEmailChannel(defaultCustomer.getUuid(), ALERT_CHANNEL_NAME);
    AlertChannelEmailParams params = (AlertChannelEmailParams) channel.getParams();
    params.setTitleTemplate(TITLE_TEMPLATE);
    params.setTextTemplate(TEXT_TEMPLATE);
    channel.setParams(params);
    channel.save();
    return channel;
  }

  private AlertChannel createEmailChannelWithEmptyTemplates() {
    return ModelFactory.createEmailChannel(defaultCustomer.getUuid(), ALERT_CHANNEL_NAME);
  }
}
