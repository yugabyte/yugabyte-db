// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts.impl;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertReceiverSlackParams;
import com.yugabyte.yw.common.alerts.YWNotificationException;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertReceiver;
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
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertReceiverSlackTest extends FakeDBApplication {

  private static final String SLACK_TEST_PATH = "/here/is/path";

  private Customer defaultCustomer;

  @InjectMocks private AlertReceiverSlack ars;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Test
  public void test() throws YWNotificationException, IOException, InterruptedException {
    try (MockWebServer server = new MockWebServer()) {
      server.start();
      HttpUrl baseUrl = server.url(SLACK_TEST_PATH);
      server.enqueue(new MockResponse().setBody("{\"status\":\"ok\"}"));

      AlertReceiver receiver = new AlertReceiver();
      AlertReceiverSlackParams params = new AlertReceiverSlackParams();
      params.username = "Slack Bot";
      params.webhookUrl = baseUrl.toString();
      receiver.setParams(params);

      Alert alert = ModelFactory.createAlert(defaultCustomer);
      ars.sendNotification(defaultCustomer, alert, receiver);

      RecordedRequest request = server.takeRequest();
      assertThat(request.getPath(), is(SLACK_TEST_PATH));
      assertThat(
          request.getBody().readString(Charset.defaultCharset()),
          equalTo(
              "{\"username\":\"Slack Bot\","
                  + "\"text\":\"*Yugabyte Platform Alert - <[test@customer.com][tc]>*\\n"
                  + "Common failure for customer 'test@customer.com', state: firing\\n"
                  + "Failure details:\\n\\nUniverse on fire!\",\"icon_url\":null}"));
    }
  }
}
