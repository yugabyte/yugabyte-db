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
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
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
public class AlertChannelServiceTest extends FakeDBApplication {

  private static final String CHANNEL_NAME = "Test Channel";

  private UUID defaultCustomerUuid;

  private AlertChannelService alertChannelService;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
    alertChannelService = new AlertChannelService();
  }

  @Test
  public void testCreateAndGet() {
    AlertChannelSlackParams slackParams =
        (AlertChannelSlackParams) AlertUtils.createParamsInstance(ChannelType.Slack);
    slackParams.username = "username";
    slackParams.webhookUrl = "http://google.com";

    AlertChannel channel =
        new AlertChannel()
            .setCustomerUUID(defaultCustomerUuid)
            .setName(CHANNEL_NAME)
            .setParams(slackParams);
    alertChannelService.save(channel);

    AlertChannel fromDb = alertChannelService.get(defaultCustomerUuid, channel.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(channel));

    // Check get for random UUID - should return null.
    assertThat(alertChannelService.get(defaultCustomerUuid, UUID.randomUUID()), nullValue());
  }

  @Test
  public void testGetOrBadRequest() {
    // Happy path.
    AlertChannel channel =
        ModelFactory.createAlertChannel(
            defaultCustomerUuid, CHANNEL_NAME, AlertUtils.createParamsInstance(ChannelType.Slack));

    AlertChannel fromDb =
        alertChannelService.getOrBadRequest(defaultCustomerUuid, channel.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(channel));

    // Should raise an exception for random UUID.
    final UUID uuid = UUID.randomUUID();
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertChannelService.getOrBadRequest(defaultCustomerUuid, uuid);
            });
    assertThat(exception.getMessage(), equalTo("Invalid Alert Channel UUID: " + uuid));
  }

  @Test
  public void testGetOrBadRequest_List() {
    // Happy path.
    AlertChannel channel1 =
        ModelFactory.createAlertChannel(
            defaultCustomerUuid,
            CHANNEL_NAME + " 1",
            AlertUtils.createParamsInstance(ChannelType.Slack));
    AlertChannel channel2 =
        ModelFactory.createAlertChannel(
            defaultCustomerUuid,
            CHANNEL_NAME + " 2",
            AlertUtils.createParamsInstance(ChannelType.Slack));

    List<AlertChannel> fromDb =
        alertChannelService.getOrBadRequest(
            defaultCustomerUuid, Arrays.asList(channel1.getUuid(), channel2.getUuid()));
    assertThat(fromDb, containsInAnyOrder(channel1, channel2));

    // Should raise an exception for random UUID.
    List<UUID> uuidsToCheck = new ArrayList<>();
    final UUID uuid1 = UUID.randomUUID();
    final UUID uuid2 = UUID.randomUUID();
    uuidsToCheck.add(channel1.getUuid());
    uuidsToCheck.add(uuid1);
    uuidsToCheck.add(channel2.getUuid());
    uuidsToCheck.add(uuid2);

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertChannelService.getOrBadRequest(defaultCustomerUuid, uuidsToCheck);
            });
    assertThat(
        exception.getMessage(),
        anyOf(
            equalTo("Invalid Alert Channel UUID: " + uuid1 + ", " + uuid2),
            equalTo("Invalid Alert Channel UUID: " + uuid2 + ", " + uuid1)));
  }

  @Test
  public void testList() {
    // First customer with two channels.
    AlertChannel channel1 =
        ModelFactory.createAlertChannel(
            defaultCustomerUuid,
            CHANNEL_NAME + " 1",
            AlertUtils.createParamsInstance(ChannelType.Email));
    AlertChannel channel2 =
        ModelFactory.createAlertChannel(
            defaultCustomerUuid,
            CHANNEL_NAME + " 2",
            AlertUtils.createParamsInstance(ChannelType.Slack));

    // Second customer with one channel.
    UUID newCustomerUUID = ModelFactory.testCustomer().uuid;
    ModelFactory.createAlertChannel(
        newCustomerUUID, CHANNEL_NAME, AlertUtils.createParamsInstance(ChannelType.Slack));

    List<AlertChannel> channels = alertChannelService.list(defaultCustomerUuid);
    assertThat(channels, containsInAnyOrder(channel1, channel2));

    channels = alertChannelService.list(newCustomerUUID);
    assertThat(channels.size(), is(1));

    // Third customer, without alert channels.
    assertThat(alertChannelService.list(UUID.randomUUID()).size(), is(0));
  }

  @Test
  public void testValidateChannel_EmptyParams() {
    AlertChannel channel = new AlertChannel().setName(CHANNEL_NAME);

    try {
      alertChannelService.validate(channel);
      fail("YWValidateException is expected.");
    } catch (PlatformValidationException e) {
      assertThat(e.getMessage(), is("Incorrect parameters in AlertChannel."));
    }
  }

  @Test
  public void testValidateChannel_HappyPath() throws PlatformValidationException {
    alertChannelService.validate(
        ModelFactory.createEmailChannel(defaultCustomerUuid, CHANNEL_NAME));
  }

  @Test
  public void testUpdate() {
    AlertChannel channel =
        ModelFactory.createAlertChannel(
            defaultCustomerUuid,
            CHANNEL_NAME + " 1",
            AlertUtils.createParamsInstance(ChannelType.Slack));

    AlertChannel updatedChannel = alertChannelService.get(defaultCustomerUuid, channel.getUuid());
    updatedChannel.setName(CHANNEL_NAME);

    AlertChannelEmailParams params = new AlertChannelEmailParams();
    params.recipients = Collections.singletonList("test@test.com");
    params.smtpData = EmailFixtures.createSmtpData();
    updatedChannel.setParams(params);

    alertChannelService.save(updatedChannel);

    AlertChannel updatedFromDb = alertChannelService.get(defaultCustomerUuid, channel.getUuid());
    assertThat(updatedChannel, equalTo(updatedFromDb));
  }

  @Test
  public void testSave_DuplicateName_Fail() {
    AlertChannel channel =
        ModelFactory.createAlertChannel(
            defaultCustomerUuid,
            CHANNEL_NAME + " 1",
            AlertUtils.createParamsInstance(ChannelType.Slack));

    ModelFactory.createAlertChannel(
        defaultCustomerUuid,
        CHANNEL_NAME + " 2",
        AlertUtils.createParamsInstance(ChannelType.Slack));

    AlertChannel updatedChannel = alertChannelService.get(defaultCustomerUuid, channel.getUuid());
    // Setting duplicate name.
    updatedChannel.setName(CHANNEL_NAME + " 2");

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertChannelService.save(updatedChannel);
            });
    assertThat(
        exception.getMessage(),
        equalTo(
            "Unable to create/update alert channel:"
                + " Alert channel with such name already exists."));
  }

  @Test
  public void testSave_LongName_Fail() {
    StringBuilder longName = new StringBuilder();
    while (longName.length() < AlertChannel.MAX_NAME_LENGTH / 4) {
      longName.append(CHANNEL_NAME);
    }

    AlertChannel channel =
        new AlertChannel()
            .setCustomerUUID(defaultCustomerUuid)
            .setName(longName.toString())
            .setParams(AlertUtils.createParamsInstance(ChannelType.Slack));

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertChannelService.save(channel);
            });
    assertThat(
        exception.getMessage(),
        equalTo("Unable to create/update alert channel: Name length (63) is exceeded."));
  }
}
