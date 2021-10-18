// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertUtilsTest extends FakeDBApplication {

  private static final String TITLE_TEMPLATE = "<b>Title template</b>";

  private static final String TEXT_TEMPLATE = "<html>Text template is here</html>";

  private static final String ALERT_CHANNEL_NAME = "Test AlertChannel";

  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  private AlertChannel createEmailChannel() {
    AlertChannelEmailParams params = new AlertChannelEmailParams();
    params.recipients = Arrays.asList("test@test.com", "me@google.com");
    params.textTemplate = TEXT_TEMPLATE;
    params.titleTemplate = TITLE_TEMPLATE;
    params.smtpData = EmailFixtures.createSmtpData();
    return ModelFactory.createAlertChannel(defaultCustomer.uuid, ALERT_CHANNEL_NAME, params);
  }

  private AlertChannel createEmailChannelWithEmptyTemplates() {
    AlertChannel channel = createEmailChannel();
    AlertChannelEmailParams params = (AlertChannelEmailParams) channel.getParams();
    params.titleTemplate = null;
    params.textTemplate = null;
    channel.setParams(params);
    channel.save();
    return channel;
  }

  @Test
  public void testFromDB_Email() {
    AlertChannel channel = createEmailChannel();
    AlertChannel fromDb = AlertChannel.get(defaultCustomer.uuid, channel.getUuid());
    assertNotNull(fromDb);
    assertEquals(channel, fromDb);
  }

  @Test
  public void testFromDB_Slack() {
    AlertChannelSlackParams params = new AlertChannelSlackParams();
    params.textTemplate = TEXT_TEMPLATE;
    params.titleTemplate = TITLE_TEMPLATE;

    params.username = "username";
    params.webhookUrl = "hook-url";
    params.iconUrl = "icon-url";

    AlertChannel channel =
        ModelFactory.createAlertChannel(defaultCustomer.uuid, ALERT_CHANNEL_NAME, params);
    AlertChannel fromDb = AlertChannel.get(defaultCustomer.uuid, channel.getUuid());
    assertNotNull(fromDb);
    assertEquals(channel, fromDb);
  }

  @Test
  public void testGetNotificationTitle_TemplateInChannel() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    alert.setDefinitionUuid(definition.getUuid());
    AlertChannel channel = createEmailChannel();

    assertEquals(
        channel.getParams().titleTemplate, AlertUtils.getNotificationTitle(alert, channel));
  }

  @Test
  public void testGetNotificationTitle_DefaultTitle() {
    Alert alert = ModelFactory.createAlert(defaultCustomer);
    AlertChannel channel = createEmailChannelWithEmptyTemplates();

    assertEquals(
        String.format(AlertUtils.DEFAULT_ALERT_NOTIFICATION_TITLE, defaultCustomer.getTag()),
        AlertUtils.getNotificationTitle(alert, channel));
  }

  @Test
  public void testGetNotificationText_TemplateInChannel() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    AlertChannel channel = createEmailChannel();
    assertEquals(channel.getParams().textTemplate, AlertUtils.getNotificationText(alert, channel));
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
            .getEffectiveLabels(configuration, AlertConfiguration.Severity.SEVERE)
            .stream()
            .map(l -> new AlertLabel(l.getName(), l.getValue()))
            .collect(Collectors.toList());
    alert.setLabels(labels);
    AlertChannel channel = createEmailChannelWithEmptyTemplates();

    AlertTemplateSubstitutor<Alert> substitutor = new AlertTemplateSubstitutor<>(alert);
    assertEquals(
        substitutor.replace(AlertUtils.DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE)
            + "\n\n"
            + alert.getMessage(),
        AlertUtils.getNotificationText(alert, channel));
  }
}
