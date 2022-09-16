// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertChannelSlackParams;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.Customer;
import java.io.IOException;
import java.nio.charset.Charset;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertChannelSlackTest extends FakeDBApplication {

  @Rule public ExpectedException exceptionGrabber = ExpectedException.none();

  private static final String SLACK_TEST_PATH = "/here/is/path";

  private Customer defaultCustomer;

  @InjectMocks private AlertChannelSlack ars;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
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
      ars.sendNotification(defaultCustomer, alert, channel);

      RecordedRequest request = server.takeRequest();
      assertThat(request.getPath(), is(SLACK_TEST_PATH));
      assertThat(
          request.getBody().readString(Charset.defaultCharset()),
          equalTo(
              "{\"username\":\"Slack Bot\",\"text\":"
                  + "\"alertConfiguration alert with severity level 'SEVERE' for"
                  + " customer 'test@customer.com' is firing.\\n\\n"
                  + "Universe on fire!\",\"icon_url\":null}"));
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

      exceptionGrabber.expect(PlatformNotificationException.class);
      ars.sendNotification(defaultCustomer, alert, channel);
    }
  }
}
