// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertChannelWebHookParams;
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
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class AlertChannelWebhookTest extends FakeDBApplication {

  @Rule public ExpectedException exceptionGrabber = ExpectedException.none();

  private static final String WEBHOOK_TEST_PATH = "/here/is/path";

  private Customer defaultCustomer;

  @InjectMocks private AlertChannelWebHook channel;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Test
  public void test() throws PlatformNotificationException, IOException, InterruptedException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url(WEBHOOK_TEST_PATH);
      server.enqueue(new MockResponse().setBody("{\"status\":\"ok\"}"));

      AlertChannel channelConfig = new AlertChannel();
      channelConfig.setName("Channel name");
      AlertChannelWebHookParams params = new AlertChannelWebHookParams();
      params.setWebhookUrl(baseUrl.toString());
      channelConfig.setParams(params);

      Alert alert = ModelFactory.createAlert(defaultCustomer);
      channel.sendNotification(defaultCustomer, alert, channelConfig);

      RecordedRequest request = server.takeRequest();
      assertThat(request.getPath(), is(WEBHOOK_TEST_PATH));

      String requestBody = request.getBody().readString(Charset.defaultCharset());
      JsonNode requestJson = Json.parse(requestBody);
      assertThat(requestJson.get("receiver").asText(), equalTo("Channel name"));
      assertThat(requestJson.get("status").asText(), equalTo("firing"));
      assertThat(requestJson.get("version").asText(), equalTo("4"));
      assertThat(
          requestJson.get("groupKey").asText(),
          equalTo(
              "{}:{configuration_uuid:\\\""
                  + alert.getConfigurationUuid()
                  + "\\\", "
                  + "definition_name:\\\"Alert 1\\\"}"));
      assertThat(requestJson.get("commonLabels").isObject(), equalTo(true));
      assertThat(requestJson.get("commonAnnotations").isObject(), equalTo(true));

      JsonNode alertJson = requestJson.get("alerts").get(0);
      assertThat(alertJson.get("status").asText(), equalTo("firing"));
      assertThat(alertJson.get("labels"), equalTo(requestJson.get("commonLabels")));
      assertThat(alertJson.get("annotations"), equalTo(requestJson.get("commonAnnotations")));
      assertThat(alertJson.get("startsAt").asText(), notNullValue());
      assertThat(alertJson.get("endsAt").isNull(), equalTo(true));
      assertThat(alertJson.get("status").asText(), equalTo("firing"));

      JsonNode groupLabelsJson = requestJson.get("groupLabels");
      assertThat(
          groupLabelsJson.get("configuration_uuid").asText(),
          equalTo(alert.getConfigurationUuid().toString()));
      assertThat(groupLabelsJson.get("definition_name").asText(), equalTo(alert.getName()));
    }
  }

  @Test
  public void testFailure() throws IOException, PlatformNotificationException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url(WEBHOOK_TEST_PATH);
      server.enqueue(new MockResponse().setResponseCode(500).setBody("{\"error\":\"not_ok\"}"));

      AlertChannel channelConfig = new AlertChannel();
      AlertChannelWebHookParams params = new AlertChannelWebHookParams();
      params.setWebhookUrl(baseUrl.toString());
      channelConfig.setParams(params);

      Alert alert = ModelFactory.createAlert(defaultCustomer);

      exceptionGrabber.expect(PlatformNotificationException.class);
      channel.sendNotification(defaultCustomer, alert, channelConfig);
    }
  }
}
