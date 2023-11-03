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

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelTemplateService;
import com.yugabyte.yw.common.alerts.AlertChannelTemplateServiceTest;
import com.yugabyte.yw.common.alerts.AlertNotificationTemplateSubstitutor;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableService;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableServiceTest;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.common.alerts.impl.AlertChannelBase.Context;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertChannelTemplates;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.AlertTemplateVariable;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;

public class AlertChannelBaseTest extends FakeDBApplication {

  private static final String DEFAULT_ALERT_NOTIFICATION_TITLE_TEMPLATE =
      "YugabyteDB Anywhere {{ $labels.severity }} alert {{ $labels.definition_name }} "
          + "{{ $labels.alert_state }} for {{ $labels.source_name }}";

  private static final String DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE =
      "{{ $labels.definition_name }} alert with severity level '{{ $labels.severity }}' "
          + "for {{ $labels.source_type }} '{{ $labels.source_name }}' "
          + "is {{ $labels.alert_state }}.\n\n{{ $annotations.message }}";

  private static final String TITLE_TEMPLATE = "<b>Title template</b>";

  private static final String TEXT_TEMPLATE = "<html>Text template is here</html>";

  private static final String ALERT_CHANNEL_NAME = "Test AlertChannel";

  private Customer defaultCustomer;
  AlertChannelBase channelBase;

  private AlertTemplateService alertTemplateService;

  private AlertTemplateVariableService alertTemplateVariableService;

  private AlertChannelTemplateService alertChannelTemplateService;

  private AlertChannelTemplatesExt defaultTemplates;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    alertTemplateService = app.injector().instanceOf(AlertTemplateService.class);
    alertTemplateVariableService = app.injector().instanceOf(AlertTemplateVariableService.class);
    alertChannelTemplateService = app.injector().instanceOf(AlertChannelTemplateService.class);
    defaultTemplates =
        alertChannelTemplateService.getWithDefaults(defaultCustomer.getUuid(), ChannelType.Email);
    channelBase =
        new AlertChannelBase(alertTemplateVariableService) {
          @Override
          public void sendNotification(
              Customer customer,
              Alert alert,
              AlertChannel channel,
              AlertChannelTemplatesExt templates)
              throws PlatformNotificationException {
            // Do nothing
          }
        };
  }

  @Test
  public void testGetNotificationTitle_TemplateForChannelType() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    alert.setDefinitionUuid(definition.getUuid());
    AlertChannel channel = createEmailChannelWithEmptyTemplates();

    AlertChannelTemplates channelTemplates =
        AlertChannelTemplateServiceTest.createTemplates(
            defaultCustomer.getUuid(), ChannelType.Email);
    alertChannelTemplateService.save(channelTemplates);
    AlertChannelTemplatesExt customTemplates =
        alertChannelTemplateService.getWithDefaults(defaultCustomer.getUuid(), ChannelType.Email);

    Context context = getContext(channel, customTemplates);
    assertEquals(
        channelTemplates.getTitleTemplate(),
        AlertChannelBase.getNotificationTitle(alert, context, false));
  }

  @Test
  public void testGetNotificationText_VariableValues() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);
    alert.setLabel("custom_value", "\"bar\"");
    alert.setDefinitionUuid(definition.getUuid());

    AlertChannel channel = createEmailChannelWithEmptyTemplates();

    AlertTemplateVariable variable1 =
        AlertTemplateVariableServiceTest.createTestVariable(
            defaultCustomer.getUuid(), "custom_value");
    AlertTemplateVariable variable2 =
        AlertTemplateVariableServiceTest.createTestVariable(
            defaultCustomer.getUuid(), "default_value");
    alertTemplateVariableService.save(variable1);
    alertTemplateVariableService.save(variable2);
    AlertChannelTemplates channelTemplates =
        AlertChannelTemplateServiceTest.createTemplates(
            defaultCustomer.getUuid(), ChannelType.Email);
    channelTemplates.setTextTemplate("Some {{ custom_value }} and {{ default_value }} variables");
    alertChannelTemplateService.save(channelTemplates);
    AlertChannelTemplatesExt customTemplates =
        alertChannelTemplateService.getWithDefaults(defaultCustomer.getUuid(), ChannelType.Email);

    Context context = getContext(channel, customTemplates);
    assertEquals(
        "Some &quot;bar&quot; and foo variables",
        AlertChannelBase.getNotificationText(alert, context, true));
  }

  @Test
  public void testGetNotificationTitle_TemplateInChannel() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    alert.setDefinitionUuid(definition.getUuid());
    AlertChannel channel = createEmailChannel();

    Context context = getContext(channel, defaultTemplates);
    assertEquals(
        channel.getParams().getTitleTemplate(),
        AlertChannelBase.getNotificationTitle(alert, context, false));
  }

  @Test
  public void testGetNotificationTitle_DefaultTitle() {
    Alert alert = ModelFactory.createAlert(defaultCustomer);
    AlertChannel channel = createEmailChannelWithEmptyTemplates();

    Context context = getContext(channel, defaultTemplates);
    AlertNotificationTemplateSubstitutor substitutor =
        new AlertNotificationTemplateSubstitutor(
            alert, channel, context.getLabelDefaultValues(), false, false);
    assertEquals(
        substitutor.replace(DEFAULT_ALERT_NOTIFICATION_TITLE_TEMPLATE),
        AlertChannelBase.getNotificationTitle(alert, context, false));
  }

  @Test
  public void testGetNotificationText_TemplateForChannelType() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    alert.setDefinitionUuid(definition.getUuid());
    AlertChannel channel = createEmailChannelWithEmptyTemplates();

    AlertChannelTemplates channelTemplates =
        AlertChannelTemplateServiceTest.createTemplates(
            defaultCustomer.getUuid(), ChannelType.Email);
    alertChannelTemplateService.save(channelTemplates);
    AlertChannelTemplatesExt customTemplates =
        alertChannelTemplateService.getWithDefaults(defaultCustomer.getUuid(), ChannelType.Email);

    Context context = getContext(channel, customTemplates);
    assertEquals(
        customTemplates.getChannelTemplates().getTextTemplate(),
        AlertChannelBase.getNotificationText(alert, context, false));
  }

  @Test
  public void testGetNotificationText_TemplateInChannel() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    AlertChannel channel = createEmailChannel();
    Context context = getContext(channel, defaultTemplates);
    assertEquals(
        channel.getParams().getTextTemplate(),
        AlertChannelBase.getNotificationText(alert, context, false));
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

    AlertTemplateDescription alertTemplateDescription =
        alertTemplateService.getTemplateDescription(configuration.getTemplate());
    List<AlertLabel> labels =
        definition
            .getEffectiveLabels(
                alertTemplateDescription, configuration, null, AlertConfiguration.Severity.SEVERE)
            .stream()
            .map(l -> new AlertLabel(l.getName(), l.getValue()))
            .collect(Collectors.toList());
    alert.setLabels(labels);
    AlertChannel channel = createEmailChannelWithEmptyTemplates();

    Context context = getContext(channel, defaultTemplates);
    AlertNotificationTemplateSubstitutor substitutor =
        new AlertNotificationTemplateSubstitutor(
            alert, channel, context.getLabelDefaultValues(), false, false);
    assertEquals(
        substitutor.replace(DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE),
        AlertChannelBase.getNotificationText(alert, context, false));
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

  private Context getContext(AlertChannel channel, AlertChannelTemplatesExt templates) {
    return new Context(
        channel, templates, alertTemplateVariableService.list(defaultCustomer.getUuid()));
  }
}
