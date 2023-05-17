// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertChannelTemplateService;
import com.yugabyte.yw.common.alerts.AlertChannelWebHookParams;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.forms.AlertChannelTemplatesExt;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.helpers.auth.BasicAuth;
import com.yugabyte.yw.models.helpers.auth.HttpAuth;
import com.yugabyte.yw.models.helpers.auth.TokenAuth;
import java.io.IOException;
import java.nio.charset.Charset;
import java.util.function.Consumer;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.eclipse.jetty.http.HttpStatus;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class AlertChannelWebhookTest extends FakeDBApplication {

  private static final String WEBHOOK_TEST_PATH = "/here/is/path";

  private Customer defaultCustomer;

  private AlertChannelWebHook channel;

  AlertChannelTemplateService alertChannelTemplateService;

  AlertChannelTemplatesExt alertChannelTemplatesExt;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    channel = app.injector().instanceOf(AlertChannelWebHook.class);

    alertChannelTemplateService = app.injector().instanceOf(AlertChannelTemplateService.class);
    alertChannelTemplatesExt =
        alertChannelTemplateService.getWithDefaults(defaultCustomer.getUuid(), ChannelType.WebHook);
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
      channel.sendNotification(defaultCustomer, alert, channelConfig, alertChannelTemplatesExt);

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
                  + "definition_name:\\\"alertConfiguration\\\"}"));
      assertThat(requestJson.get("commonLabels").isObject(), equalTo(true));
      assertThat(requestJson.get("commonAnnotations").isObject(), equalTo(true));

      JsonNode alertJson = requestJson.get("alerts").get(0);
      assertThat(alertJson.get("status").asText(), equalTo("firing"));
      assertThat(alertJson.get("labels"), equalTo(requestJson.get("commonLabels")));
      assertThat(alertJson.get("annotations"), equalTo(requestJson.get("commonAnnotations")));
      assertThat(alertJson.get("startsAt").asText(), notNullValue());
      assertTrue(alertJson.get("endsAt").isNull());
      assertThat(alertJson.get("status").asText(), equalTo("firing"));

      JsonNode groupLabelsJson = requestJson.get("groupLabels");
      assertThat(
          groupLabelsJson.get("configuration_uuid").asText(),
          equalTo(alert.getConfigurationUuid().toString()));
      assertThat(groupLabelsJson.get("definition_name").asText(), equalTo("alertConfiguration"));
    }
  }

  @Test
  public void test204() throws PlatformNotificationException, IOException, InterruptedException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url(WEBHOOK_TEST_PATH);
      server.enqueue(new MockResponse().setResponseCode(HttpStatus.NO_CONTENT_204));

      AlertChannel channelConfig = new AlertChannel();
      channelConfig.setName("Channel name");
      AlertChannelWebHookParams params = new AlertChannelWebHookParams();
      params.setWebhookUrl(baseUrl.toString());
      channelConfig.setParams(params);

      Alert alert = ModelFactory.createAlert(defaultCustomer);
      channel.sendNotification(defaultCustomer, alert, channelConfig, alertChannelTemplatesExt);
      // In case no Exception is raised - notification was sent successfully
    }
  }

  @Test
  public void testBasicAuth()
      throws PlatformNotificationException, IOException, InterruptedException {
    BasicAuth basicAuth = new BasicAuth();
    basicAuth.setUsername("testUser");
    basicAuth.setPassword("testPassword");
    testAuth(
        basicAuth,
        recordedRequest ->
            assertThat(
                recordedRequest.getHeader("Authorization"),
                equalTo("Basic dGVzdFVzZXI6dGVzdFBhc3N3b3Jk")));
  }

  @Test
  public void testTokenAuth()
      throws PlatformNotificationException, IOException, InterruptedException {
    TokenAuth tokenAuth = new TokenAuth();
    tokenAuth.setTokenHeader("testHeader");
    tokenAuth.setTokenValue("testValue");
    testAuth(
        tokenAuth,
        recordedRequest ->
            assertThat(recordedRequest.getHeader("testHeader"), equalTo("testValue")));
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

      assertThat(
          () ->
              channel.sendNotification(
                  defaultCustomer, alert, channelConfig, alertChannelTemplatesExt),
          thrown(
              PlatformNotificationException.class,
              "Error sending WebHook message for alert Alert 1: "
                  + "error response 500 received with body {\"error\":\"not_ok\"}"));
    }
  }

  private void testAuth(HttpAuth auth, Consumer<RecordedRequest> asserts)
      throws PlatformNotificationException, IOException, InterruptedException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url(WEBHOOK_TEST_PATH);
      server.enqueue(new MockResponse().setBody("{\"status\":\"ok\"}"));

      AlertChannel channelConfig = new AlertChannel();
      channelConfig.setName("Channel name");
      AlertChannelWebHookParams params = new AlertChannelWebHookParams();
      params.setWebhookUrl(baseUrl.toString());
      params.setHttpAuth(auth);
      channelConfig.setParams(params);

      Alert alert = ModelFactory.createAlert(defaultCustomer);
      channel.sendNotification(defaultCustomer, alert, channelConfig, alertChannelTemplatesExt);

      RecordedRequest request = server.takeRequest();
      assertThat(request.getPath(), is(WEBHOOK_TEST_PATH));
      asserts.accept(request);
    }
  }
}
