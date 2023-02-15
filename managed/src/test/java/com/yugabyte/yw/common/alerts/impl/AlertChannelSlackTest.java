// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.alerts.AlertChannelSlackParams;
import com.yugabyte.yw.common.alerts.AlertChannelTemplateService;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.Customer;
import java.io.IOException;
import java.nio.charset.Charset;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertChannelSlackTest extends FakeDBApplication {

  private static final String SLACK_TEST_PATH = "/here/is/path";

  private Customer defaultCustomer;

  private AlertChannelSlack alertChannelSlack;

  AlertChannelTemplateService alertChannelTemplateService;

  AlertChannelTemplatesExt alertChannelTemplatesExt;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    alertChannelSlack = app.injector().instanceOf(AlertChannelSlack.class);

    alertChannelTemplateService = app.injector().instanceOf(AlertChannelTemplateService.class);
    alertChannelTemplatesExt =
        alertChannelTemplateService.getWithDefaults(defaultCustomer.getUuid(), ChannelType.Slack);
  }

  @Test
  public void test() throws PlatformNotificationException, IOException, InterruptedException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url(SLACK_TEST_PATH);
      server.enqueue(new MockResponse().setBody("{\"status\":\"ok\"}"));

      AlertChannel channel = new AlertChannel();
      AlertChannelSlackParams params = new AlertChannelSlackParams();
      params.setUsername("Slack Bot");
      params.setWebhookUrl(baseUrl.toString());
      channel.setParams(params);

      Alert alert = ModelFactory.createAlert(defaultCustomer);
      alertChannelSlack.sendNotification(defaultCustomer, alert, channel, alertChannelTemplatesExt);

      RecordedRequest request = server.takeRequest();
      assertThat(request.getPath(), is(SLACK_TEST_PATH));
      String expectedBody = TestUtils.readResource("alert/alert_notification_slack.json").trim();
      assertThat(request.getBody().readString(Charset.defaultCharset()), equalTo(expectedBody));
    }
  }

  @Test
  public void testFailure() throws PlatformNotificationException, IOException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url(SLACK_TEST_PATH);
      server.enqueue(new MockResponse().setResponseCode(500).setBody("{\"error\":\"not_ok\"}"));

      AlertChannel channel = new AlertChannel();
      AlertChannelSlackParams params = new AlertChannelSlackParams();
      params.setUsername("Slack Bot");
      params.setWebhookUrl(baseUrl.toString());
      channel.setParams(params);

      Alert alert = ModelFactory.createAlert(defaultCustomer);

      assertThat(
          () ->
              alertChannelSlack.sendNotification(
                  defaultCustomer, alert, channel, alertChannelTemplatesExt),
          thrown(
              PlatformNotificationException.class,
              "Error sending Slack message for alert Alert 1: "
                  + "error response 500 received with body {\"error\":\"not_ok\"}"));
    }
  }
}
