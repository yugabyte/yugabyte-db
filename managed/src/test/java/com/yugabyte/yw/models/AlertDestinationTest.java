// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.notNullValue;
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import java.util.Collections;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertDestinationTest extends FakeDBApplication {

  private static final String ALERT_DESTINATION_NAME = "Test AlertDestination";

  private Customer defaultCustomer;

  private AlertChannel channel;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    channel = ModelFactory.createEmailChannel(defaultCustomer.getUuid(), "Test AlertChannel");
  }

  @Test
  public void testGet() {
    AlertDestination destination =
        ModelFactory.createAlertDestination(
            defaultCustomer.getUuid(), ALERT_DESTINATION_NAME, Collections.singletonList(channel));

    AlertDestination fromDb =
        AlertDestination.get(defaultCustomer.getUuid(), destination.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(destination));
  }

  @Test
  public void testNameUniquenessCheck() {
    Customer secondCustomer = ModelFactory.testCustomer();
    ModelFactory.createAlertDestination(
        defaultCustomer.getUuid(), ALERT_DESTINATION_NAME, Collections.singletonList(channel));
    ModelFactory.createAlertDestination(
        secondCustomer.getUuid(), ALERT_DESTINATION_NAME, Collections.singletonList(channel));
    try {
      ModelFactory.createAlertDestination(
          defaultCustomer.getUuid(), ALERT_DESTINATION_NAME, Collections.singletonList(channel));
      fail("Missed expected exception.");
    } catch (Exception e) {
    }
  }
}
