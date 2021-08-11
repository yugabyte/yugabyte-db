// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverParams;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.models.AlertReceiver.TargetType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertReceiverTest extends FakeDBApplication {

  private static final String RECEIVER_NAME = "Test Receiver";

  private UUID defaultCustomerUuid;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
  }

  @Test
  public void testGetSetParams() {
    AlertReceiver receiver =
        ModelFactory.createAlertReceiver(
            defaultCustomerUuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));

    assertNull(
        AlertReceiver.get(defaultCustomerUuid, receiver.getUuid()).getParams().titleTemplate);
    assertNull(AlertReceiver.get(defaultCustomerUuid, receiver.getUuid()).getParams().textTemplate);

    AlertReceiverParams params = new AlertReceiverEmailParams();
    params.titleTemplate = "title";
    params.textTemplate = "body";
    receiver.setParams(params);
    receiver.save();

    assertEquals(
        "title",
        AlertReceiver.get(defaultCustomerUuid, receiver.getUuid()).getParams().titleTemplate);
    assertEquals(
        "body",
        AlertReceiver.get(defaultCustomerUuid, receiver.getUuid()).getParams().textTemplate);
  }

  @Test
  public void testNameUniquenessCheck() {
    Customer secondCustomer = ModelFactory.testCustomer();
    ModelFactory.createAlertReceiver(
        defaultCustomerUuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));
    ModelFactory.createAlertReceiver(
        secondCustomer.uuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));
    try {
      ModelFactory.createAlertReceiver(
          defaultCustomerUuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));
      fail("Missed expected exception.");
    } catch (Exception e) {
    }
  }
}
