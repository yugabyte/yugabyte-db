// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.util.List;
import java.util.UUID;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverParams;
import com.yugabyte.yw.common.alerts.AlertReceiverSlackParams;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.models.AlertReceiver.TargetType;

@RunWith(MockitoJUnitRunner.class)
public class AlertReceiverTest extends FakeDBApplication {

  private Customer defaultCustomer;

  private static final String RECEIVER_NAME = "Test Receiver";

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Test
  public void testCreateAndGet() {
    AlertReceiver receiver =
        AlertReceiver.create(
            defaultCustomer.uuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));

    AlertReceiver fromDb = AlertReceiver.get(defaultCustomer.uuid, receiver.getUuid());
    assertTrue(fromDb.getParams() instanceof AlertReceiverSlackParams);
  }

  @Test
  public void testGetSetParams() {
    AlertReceiver receiver =
        AlertReceiver.create(
            defaultCustomer.uuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));

    assertNull(
        AlertReceiver.get(defaultCustomer.uuid, receiver.getUuid()).getParams().titleTemplate);
    assertNull(
        AlertReceiver.get(defaultCustomer.uuid, receiver.getUuid()).getParams().textTemplate);

    AlertReceiverParams params = new AlertReceiverEmailParams();
    params.titleTemplate = "title";
    params.textTemplate = "body";
    receiver.setParams(params);
    receiver.save();

    assertEquals(
        "title",
        AlertReceiver.get(defaultCustomer.uuid, receiver.getUuid()).getParams().titleTemplate);
    assertEquals(
        "body",
        AlertReceiver.get(defaultCustomer.uuid, receiver.getUuid()).getParams().textTemplate);
  }

  @Test
  public void testGetOrBadRequest() {
    // Happy path.
    AlertReceiver receiver =
        AlertReceiver.create(
            defaultCustomer.uuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));

    AlertReceiver fromDb = AlertReceiver.getOrBadRequest(defaultCustomer.uuid, receiver.getUuid());
    assertTrue(fromDb.getParams() instanceof AlertReceiverSlackParams);

    // A receiver doesn't exist, an exception is thrown.
    try {
      AlertReceiver.getOrBadRequest(defaultCustomer.uuid, UUID.randomUUID());
      fail("YWServiceException was expected but is absent.");
    } catch (YWServiceException e) {
    }
  }

  @Test
  public void testList() {
    // First customer with two receivers.
    AlertReceiver receiver1 =
        AlertReceiver.create(
            defaultCustomer.uuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Email));
    AlertReceiver receiver2 =
        AlertReceiver.create(
            defaultCustomer.uuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));

    // Second customer with one receiver.
    UUID newCustomerUUID = ModelFactory.testCustomer().uuid;
    AlertReceiver.create(
        newCustomerUUID, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));

    List<AlertReceiver> receivers = AlertReceiver.list(defaultCustomer.uuid);
    assertEquals(2, receivers.size());
    assertTrue(receivers.contains(receiver1));
    assertTrue(receivers.contains(receiver2));

    receivers = AlertReceiver.list(newCustomerUUID);
    assertEquals(1, receivers.size());

    // Third customer, without alert receivers.
    assertEquals(0, AlertReceiver.list(UUID.randomUUID()).size());
  }
}
