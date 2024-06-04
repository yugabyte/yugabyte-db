// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertChannelEmailParams;
import com.yugabyte.yw.common.alerts.AlertChannelParams;
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
    AlertChannel channel = ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME);

    assertNull(
        AlertChannel.get(defaultCustomerUuid, channel.getUuid()).getParams().getTitleTemplate());
    assertNull(
        AlertChannel.get(defaultCustomerUuid, channel.getUuid()).getParams().getTextTemplate());

    AlertChannelParams params = new AlertChannelEmailParams();
    params.setTitleTemplate("title");
    params.setTextTemplate("body");
    channel.setParams(params);
    channel.save();

    assertEquals(
        "title",
        AlertChannel.get(defaultCustomerUuid, channel.getUuid()).getParams().getTitleTemplate());
    assertEquals(
        "body",
        AlertChannel.get(defaultCustomerUuid, channel.getUuid()).getParams().getTextTemplate());
  }

  @Test
  public void testNameUniquenessCheck() {
    Customer secondCustomer = ModelFactory.testCustomer();
    ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME);
    ModelFactory.createSlackChannel(secondCustomer.getUuid(), CHANNEL_NAME);
    try {
      ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME);
      fail("Missed expected exception.");
    } catch (Exception e) {
    }
  }
}
