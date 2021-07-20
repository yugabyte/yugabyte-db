// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertLabel;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertReceiver.TargetType;
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

  private static final String ALERT_RECEIVER_NAME = "Test AlertReceiver";

  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  private AlertReceiver createEmailReceiver() {
    AlertReceiverEmailParams params = new AlertReceiverEmailParams();
    params.recipients = Arrays.asList("test@test.com", "me@google.com");
    params.textTemplate = TEXT_TEMPLATE;
    params.titleTemplate = TITLE_TEMPLATE;
    params.smtpData = EmailFixtures.createSmtpData();
    return AlertReceiver.create(defaultCustomer.uuid, ALERT_RECEIVER_NAME, params);
  }

  private AlertReceiver createEmailReceiverWithEmptyTemplates() {
    AlertReceiver receiver = createEmailReceiver();
    AlertReceiverEmailParams params = (AlertReceiverEmailParams) receiver.getParams();
    params.titleTemplate = null;
    params.textTemplate = null;
    receiver.setParams(params);
    receiver.save();
    return receiver;
  }

  @Test
  public void testFromDB_Email() {
    AlertReceiver receiver = createEmailReceiver();
    AlertReceiver fromDb = AlertReceiver.get(defaultCustomer.uuid, receiver.getUuid());
    assertNotNull(fromDb);
    assertEquals(receiver, fromDb);
  }

  @Test
  public void testFromDB_Slack() {
    AlertReceiverSlackParams params = new AlertReceiverSlackParams();
    params.textTemplate = TEXT_TEMPLATE;
    params.titleTemplate = TITLE_TEMPLATE;

    params.channel = "channel";
    params.webhookUrl = "hook-url";
    params.iconUrl = "icon-url";

    AlertReceiver receiver =
        AlertReceiver.create(defaultCustomer.uuid, ALERT_RECEIVER_NAME, params);
    AlertReceiver fromDb = AlertReceiver.get(defaultCustomer.uuid, receiver.getUuid());
    assertNotNull(fromDb);
    assertEquals(receiver, fromDb);
  }

  @Test
  public void testCreateParamsInstance() {
    for (TargetType targetType : TargetType.values()) {
      assertNotNull(AlertUtils.createParamsInstance(targetType));
    }
  }

  @Test
  public void testGetNotificationTitle_TemplateInReceiver() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    alert.setDefinitionUuid(definition.getUuid());
    AlertReceiver receiver = createEmailReceiver();

    assertEquals(
        receiver.getParams().titleTemplate, AlertUtils.getNotificationTitle(alert, receiver));
  }

  @Test
  public void testGetNotificationTitle_DefaultTitle() {
    Alert alert = ModelFactory.createAlert(defaultCustomer);
    AlertReceiver receiver = createEmailReceiverWithEmptyTemplates();

    assertEquals(
        String.format(AlertUtils.DEFAULT_ALERT_NOTIFICATION_TITLE, defaultCustomer.getTag()),
        AlertUtils.getNotificationTitle(alert, receiver));
  }

  @Test
  public void testGetNotificationText_TemplateInReceiver() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinition definition = ModelFactory.createAlertDefinition(defaultCustomer, universe);
    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);

    AlertReceiver receiver = createEmailReceiver();
    assertEquals(
        receiver.getParams().textTemplate, AlertUtils.getNotificationText(alert, receiver));
  }

  @Test
  public void testGetNotificationText_DefaultTemplate() {
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Alert alert = ModelFactory.createAlert(defaultCustomer, universe);
    AlertReceiver receiver = createEmailReceiverWithEmptyTemplates();

    assertEquals(
        AlertUtils.getDefaultNotificationText(alert),
        AlertUtils.getNotificationText(alert, receiver));
  }

  @Test
  public void testGetNotificationText_TemplateInAlert() {
    Universe universe = ModelFactory.createUniverse();
    AlertDefinitionGroup group = ModelFactory.createAlertDefinitionGroup(defaultCustomer, universe);
    AlertDefinition definition =
        ModelFactory.createAlertDefinition(defaultCustomer, universe, group);

    Alert alert = ModelFactory.createAlert(defaultCustomer, definition);
    alert.setDefinitionUuid(definition.getUuid());

    List<AlertLabel> labels =
        definition
            .getEffectiveLabels(group, AlertDefinitionGroup.Severity.SEVERE)
            .stream()
            .map(l -> new AlertLabel(l.getName(), l.getValue()))
            .collect(Collectors.toList());
    alert.setLabels(labels);
    AlertReceiver receiver = createEmailReceiverWithEmptyTemplates();

    AlertTemplateSubstitutor<Alert> substitutor = new AlertTemplateSubstitutor<>(alert);
    assertEquals(
        substitutor.replace(AlertUtils.DEFAULT_ALERT_NOTIFICATION_TEXT_TEMPLATE),
        AlertUtils.getNotificationText(alert, receiver));
  }

  @Test
  public void testValidateReceiver_EmptyParams() {
    AlertReceiver receiver = new AlertReceiver();
    try {
      AlertUtils.validate(receiver);
      fail("YWValidateException is expected.");
    } catch (YWValidateException e) {
      assertEquals("Incorrect parameters in AlertReceiver.", e.getMessage());
    }
  }

  @Test
  public void testValidateReceiver_HappyPath() throws YWValidateException {
    AlertUtils.validate(createEmailReceiver());
  }
}
