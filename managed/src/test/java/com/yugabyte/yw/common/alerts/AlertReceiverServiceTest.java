// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.anyOf;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;

import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertReceiver.TargetType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertReceiverServiceTest extends FakeDBApplication {

  private static final String RECEIVER_NAME = "Test Receiver";

  private UUID defaultCustomerUuid;

  private AlertReceiverService alertReceiverService;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
    alertReceiverService = new AlertReceiverService();
  }

  @Test
  public void testCreateAndGet() {
    AlertReceiverSlackParams slackParams =
        (AlertReceiverSlackParams) AlertUtils.createParamsInstance(TargetType.Slack);
    slackParams.username = "username";
    slackParams.webhookUrl = "http://google.com";

    AlertReceiver receiver =
        new AlertReceiver()
            .setCustomerUUID(defaultCustomerUuid)
            .setName(RECEIVER_NAME)
            .setParams(slackParams);
    alertReceiverService.save(receiver);

    AlertReceiver fromDb = alertReceiverService.get(defaultCustomerUuid, receiver.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(receiver));

    // Check get for random UUID - should return null.
    assertThat(alertReceiverService.get(defaultCustomerUuid, UUID.randomUUID()), nullValue());
  }

  @Test
  public void testGetOrBadRequest() {
    // Happy path.
    AlertReceiver receiver =
        ModelFactory.createAlertReceiver(
            defaultCustomerUuid, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));

    AlertReceiver fromDb =
        alertReceiverService.getOrBadRequest(defaultCustomerUuid, receiver.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(receiver));

    // Should raise an exception for random UUID.
    final UUID uuid = UUID.randomUUID();
    YWServiceException exception =
        assertThrows(
            YWServiceException.class,
            () -> {
              alertReceiverService.getOrBadRequest(defaultCustomerUuid, uuid);
            });
    assertThat(exception.getMessage(), equalTo("Invalid Alert Receiver UUID: " + uuid));
  }

  @Test
  public void testGetOrBadRequest_List() {
    // Happy path.
    AlertReceiver receiver1 =
        ModelFactory.createAlertReceiver(
            defaultCustomerUuid,
            RECEIVER_NAME + " 1",
            AlertUtils.createParamsInstance(TargetType.Slack));
    AlertReceiver receiver2 =
        ModelFactory.createAlertReceiver(
            defaultCustomerUuid,
            RECEIVER_NAME + " 2",
            AlertUtils.createParamsInstance(TargetType.Slack));

    List<AlertReceiver> fromDb =
        alertReceiverService.getOrBadRequest(
            defaultCustomerUuid, Arrays.asList(receiver1.getUuid(), receiver2.getUuid()));
    assertThat(fromDb, containsInAnyOrder(receiver1, receiver2));

    // Should raise an exception for random UUID.
    List<UUID> uuidsToCheck = new ArrayList<>();
    final UUID uuid1 = UUID.randomUUID();
    final UUID uuid2 = UUID.randomUUID();
    uuidsToCheck.add(receiver1.getUuid());
    uuidsToCheck.add(uuid1);
    uuidsToCheck.add(receiver2.getUuid());
    uuidsToCheck.add(uuid2);

    YWServiceException exception =
        assertThrows(
            YWServiceException.class,
            () -> {
              alertReceiverService.getOrBadRequest(defaultCustomerUuid, uuidsToCheck);
            });
    assertThat(
        exception.getMessage(),
        anyOf(
            equalTo("Invalid Alert Receiver UUID: " + uuid1 + ", " + uuid2),
            equalTo("Invalid Alert Receiver UUID: " + uuid2 + ", " + uuid1)));
  }

  @Test
  public void testList() {
    // First customer with two receivers.
    AlertReceiver receiver1 =
        ModelFactory.createAlertReceiver(
            defaultCustomerUuid,
            RECEIVER_NAME + " 1",
            AlertUtils.createParamsInstance(TargetType.Email));
    AlertReceiver receiver2 =
        ModelFactory.createAlertReceiver(
            defaultCustomerUuid,
            RECEIVER_NAME + " 2",
            AlertUtils.createParamsInstance(TargetType.Slack));

    // Second customer with one receiver.
    UUID newCustomerUUID = ModelFactory.testCustomer().uuid;
    ModelFactory.createAlertReceiver(
        newCustomerUUID, RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));

    List<AlertReceiver> receivers = alertReceiverService.list(defaultCustomerUuid);
    assertThat(receivers, containsInAnyOrder(receiver1, receiver2));

    receivers = alertReceiverService.list(newCustomerUUID);
    assertThat(receivers.size(), is(1));

    // Third customer, without alert receivers.
    assertThat(alertReceiverService.list(UUID.randomUUID()).size(), is(0));
  }

  @Test
  public void testValidateReceiver_EmptyParams() {
    AlertReceiver receiver = new AlertReceiver().setName(RECEIVER_NAME);

    try {
      alertReceiverService.validate(receiver);
      fail("YWValidateException is expected.");
    } catch (YWValidateException e) {
      assertThat(e.getMessage(), is("Incorrect parameters in AlertReceiver."));
    }
  }

  @Test
  public void testValidateReceiver_HappyPath() throws YWValidateException {
    alertReceiverService.validate(
        ModelFactory.createEmailReceiver(defaultCustomerUuid, RECEIVER_NAME));
  }

  @Test
  public void testUpdate() {
    AlertReceiver receiver =
        ModelFactory.createAlertReceiver(
            defaultCustomerUuid,
            RECEIVER_NAME + " 1",
            AlertUtils.createParamsInstance(TargetType.Slack));

    AlertReceiver updatedReceiver =
        alertReceiverService.get(defaultCustomerUuid, receiver.getUuid());
    updatedReceiver.setName(RECEIVER_NAME);

    AlertReceiverEmailParams params = new AlertReceiverEmailParams();
    params.recipients = Collections.singletonList("test@test.com");
    params.smtpData = EmailFixtures.createSmtpData();
    updatedReceiver.setParams(params);

    alertReceiverService.save(updatedReceiver);

    AlertReceiver updatedFromDb = alertReceiverService.get(defaultCustomerUuid, receiver.getUuid());
    assertThat(updatedReceiver, equalTo(updatedFromDb));
  }

  @Test
  public void testSave_DuplicateName_Fail() {
    AlertReceiver receiver =
        ModelFactory.createAlertReceiver(
            defaultCustomerUuid,
            RECEIVER_NAME + " 1",
            AlertUtils.createParamsInstance(TargetType.Slack));

    ModelFactory.createAlertReceiver(
        defaultCustomerUuid,
        RECEIVER_NAME + " 2",
        AlertUtils.createParamsInstance(TargetType.Slack));

    AlertReceiver updatedReceiver =
        alertReceiverService.get(defaultCustomerUuid, receiver.getUuid());
    // Setting duplicate name.
    updatedReceiver.setName(RECEIVER_NAME + " 2");

    YWServiceException exception =
        assertThrows(
            YWServiceException.class,
            () -> {
              alertReceiverService.save(updatedReceiver);
            });
    assertThat(
        exception.getMessage(),
        equalTo(
            "Unable to create/update alert receiver:"
                + " Alert receiver with such name already exists."));
  }

  @Test
  public void testSave_LongName_Fail() {
    StringBuilder longName = new StringBuilder();
    while (longName.length() < AlertReceiver.MAX_NAME_LENGTH / 4) {
      longName.append(RECEIVER_NAME);
    }

    AlertReceiver receiver =
        new AlertReceiver()
            .setCustomerUUID(defaultCustomerUuid)
            .setName(longName.toString())
            .setParams(AlertUtils.createParamsInstance(TargetType.Slack));

    YWServiceException exception =
        assertThrows(
            YWServiceException.class,
            () -> {
              alertReceiverService.save(receiver);
            });
    assertThat(
        exception.getMessage(),
        equalTo("Unable to create/update alert receiver: Name length (63) is exceeded."));
  }
}
