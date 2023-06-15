// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.Customer;
import java.util.Arrays;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertChannelParamsTest extends FakeDBApplication {

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
    params.setRecipients(Arrays.asList("test@test.com", "me@google.com"));
    params.setTextTemplate(TEXT_TEMPLATE);
    params.setTitleTemplate(TITLE_TEMPLATE);
    params.setSmtpData(EmailFixtures.createSmtpData());
    return ModelFactory.createAlertChannel(defaultCustomer.getUuid(), ALERT_CHANNEL_NAME, params);
  }

  @Test
  public void testFromDB_Email() {
    AlertChannel channel = createEmailChannel();
    AlertChannel fromDb = AlertChannel.get(defaultCustomer.getUuid(), channel.getUuid());
    assertNotNull(fromDb);
    assertEquals(channel, fromDb);
  }

  @Test
  public void testFromDB_Slack() {
    AlertChannelSlackParams params = new AlertChannelSlackParams();
    params.setTextTemplate(TEXT_TEMPLATE);
    params.setTitleTemplate(TITLE_TEMPLATE);

    params.setUsername("username");
    params.setIconUrl("hook-url");
    params.setIconUrl("icon-url");

    AlertChannel channel =
        ModelFactory.createAlertChannel(defaultCustomer.getUuid(), ALERT_CHANNEL_NAME, params);
    AlertChannel fromDb = AlertChannel.get(defaultCustomer.getUuid(), channel.getUuid());
    assertNotNull(fromDb);
    assertEquals(channel, fromDb);
  }
}
