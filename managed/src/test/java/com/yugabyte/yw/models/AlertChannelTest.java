// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelParams;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertChannelTest extends FakeDBApplication {

  private static final String CHANNEL_NAME = "Test Channel";

  private UUID defaultCustomerUuid;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
  }

  @Test
  public void testGetSetParams() {
    AlertChannel channel =
        ModelFactory.createAlertChannel(
            defaultCustomerUuid, CHANNEL_NAME, AlertUtils.createParamsInstance(ChannelType.Slack));

    assertNull(AlertChannel.get(defaultCustomerUuid, channel.getUuid()).getParams().titleTemplate);
    assertNull(AlertChannel.get(defaultCustomerUuid, channel.getUuid()).getParams().textTemplate);

    AlertChannelParams params = new AlertChannelEmailParams();
    params.titleTemplate = "title";
    params.textTemplate = "body";
    channel.setParams(params);
    channel.save();

    assertEquals(
        "title",
        AlertChannel.get(defaultCustomerUuid, channel.getUuid()).getParams().titleTemplate);
    assertEquals(
        "body", AlertChannel.get(defaultCustomerUuid, channel.getUuid()).getParams().textTemplate);
  }

  @Test
  public void testNameUniquenessCheck() {
    Customer secondCustomer = ModelFactory.testCustomer();
    ModelFactory.createAlertChannel(
        defaultCustomerUuid, CHANNEL_NAME, AlertUtils.createParamsInstance(ChannelType.Slack));
    ModelFactory.createAlertChannel(
        secondCustomer.uuid, CHANNEL_NAME, AlertUtils.createParamsInstance(ChannelType.Slack));
    try {
      ModelFactory.createAlertChannel(
          defaultCustomerUuid, CHANNEL_NAME, AlertUtils.createParamsInstance(ChannelType.Slack));
      fail("Missed expected exception.");
    } catch (Exception e) {
    }
  }
}
