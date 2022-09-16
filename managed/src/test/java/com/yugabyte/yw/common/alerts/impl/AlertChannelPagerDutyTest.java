// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertChannelPagerDutyParams;
import com.yugabyte.yw.common.alerts.PlatformNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.Customer;
import java.io.IOException;
import java.nio.charset.Charset;
import okhttp3.HttpUrl;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.mockwebserver.RecordedRequest;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.rules.ExpectedException;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class AlertChannelPagerDutyTest extends FakeDBApplication {

  @Rule public ExpectedException exceptionGrabber = ExpectedException.none();

  private static final String PAGERDUTY_PATH = "/test/path";

  private Customer defaultCustomer;

  @InjectMocks private AlertChannelPagerDuty channel;

  private MockWebServer server;

  @Before
  public void setUp() throws IOException {
    defaultCustomer = ModelFactory.testCustomer();
    server = new MockWebServer();
    server.start();
    HttpUrl baseUrl = server.url(PAGERDUTY_PATH);
    channel = new AlertChannelPagerDuty(baseUrl.toString());
  }

  @Test
  public void test() throws PlatformNotificationException, InterruptedException {
    server.enqueue(new MockResponse().setBody("{\"status\":\"ok\"}"));

    AlertChannel channelConfig = new AlertChannel();
    AlertChannelPagerDutyParams params = new AlertChannelPagerDutyParams();
    params.setApiKey("Some API Key");
    params.setRoutingKey("Some Routing Key");
    channelConfig.setParams(params);

    Alert alert = ModelFactory.createAlert(defaultCustomer);
    channel.sendNotification(defaultCustomer, alert, channelConfig);

    RecordedRequest request = server.takeRequest();
    assertThat(request.getPath(), is(PAGERDUTY_PATH));
    String requestBody = request.getBody().readString(Charset.defaultCharset());
    JsonNode requestJson = Json.parse(requestBody);
    assertThat(requestJson.get("routing_key").asText(), equalTo("Some Routing Key"));
    assertThat(requestJson.get("event_action").asText(), equalTo("trigger"));
    assertThat(requestJson.get("dedup_key").asText(), equalTo(alert.getUuid().toString()));
    JsonNode payloadJson = requestJson.get("payload");
    assertThat(
        payloadJson.get("summary").asText(),
        equalTo(
            "alertConfiguration alert with severity level 'SEVERE'"
                + " for customer 'test@customer.com' is firing.\n\nUniverse on fire!"));
    assertThat(payloadJson.get("source").asText(), equalTo("YB Platform test@customer.com"));
    assertThat(payloadJson.get("severity").asText(), equalTo("error"));
    assertThat(payloadJson.get("group").asText(), equalTo("UNIVERSE"));
    assertThat(
        payloadJson.get("class").asText(),
        equalTo("YugabyteDB Anywhere SEVERE alert alertConfiguration fired for test@customer.com"));
  }

  @Test
  public void testResolve() throws PlatformNotificationException, InterruptedException {
    server.enqueue(new MockResponse().setBody("{\"status\":\"ok\"}"));

    AlertChannel channelConfig = new AlertChannel();
    AlertChannelPagerDutyParams params = new AlertChannelPagerDutyParams();
    params.setApiKey("Some API Key");
    params.setRoutingKey("Some Routing Key");
    channelConfig.setParams(params);

    Alert alert = ModelFactory.createAlert(defaultCustomer);
    alert.setState(State.RESOLVED);
    channel.sendNotification(defaultCustomer, alert, channelConfig);

    RecordedRequest request = server.takeRequest();
    assertThat(request.getPath(), is(PAGERDUTY_PATH));
    String requestBody = request.getBody().readString(Charset.defaultCharset());
    JsonNode requestJson = Json.parse(requestBody);
    assertThat(requestJson.get("routing_key").asText(), equalTo("Some Routing Key"));
    assertThat(requestJson.get("event_action").asText(), equalTo("resolve"));
    assertThat(requestJson.get("dedup_key").asText(), equalTo(alert.getUuid().toString()));
  }

  @Test
  public void testFailure() throws PlatformNotificationException, IOException {
    server.enqueue(new MockResponse().setResponseCode(500).setBody("{\"error\":\"not_ok\"}"));

    AlertChannel channelConfig = new AlertChannel();
    AlertChannelPagerDutyParams params = new AlertChannelPagerDutyParams();
    params.setApiKey("Some API Key");
    params.setRoutingKey("Some Routing Key");
    channelConfig.setParams(params);

    Alert alert = ModelFactory.createAlert(defaultCustomer);

    exceptionGrabber.expect(PlatformNotificationException.class);
    channel.sendNotification(defaultCustomer, alert, channelConfig);
  }

  @After
  public void after() throws IOException {
    server.close();
  }
}
