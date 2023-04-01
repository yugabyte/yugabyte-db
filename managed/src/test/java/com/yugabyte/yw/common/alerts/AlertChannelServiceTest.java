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
import com.yugabyte.yw.common.EmailHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertChannel;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.converters.Nullable;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class AlertChannelServiceTest extends FakeDBApplication {

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  private static final String CHANNEL_NAME = "Test Channel";

  private UUID defaultCustomerUuid;

  private AlertChannelService alertChannelService;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
    alertChannelService = app.injector().instanceOf(AlertChannelService.class);
  }

  @Test
  public void testCreateAndGet() {
    AlertChannelSlackParams slackParams = new AlertChannelSlackParams();
    slackParams.setUsername("username");
    slackParams.setWebhookUrl("http://google.com");

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
    AlertChannel channel = ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME);

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
        ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME + " 1");
    AlertChannel channel2 =
        ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME + " 2");

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
        ModelFactory.createEmailChannel(defaultCustomerUuid, CHANNEL_NAME + " 1");
    AlertChannel channel2 =
        ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME + " 2");

    // Second customer with one channel.
    UUID newCustomerUUID = ModelFactory.testCustomer().getUuid();
    ModelFactory.createSlackChannel(newCustomerUUID, CHANNEL_NAME);

    List<AlertChannel> channels = alertChannelService.list(defaultCustomerUuid);
    assertThat(channels, containsInAnyOrder(channel1, channel2));

    channels = alertChannelService.list(newCustomerUUID);
    assertThat(channels.size(), is(1));

    // Third customer, without alert channels.
    assertThat(alertChannelService.list(UUID.randomUUID()).size(), is(0));
  }

  @Test
  public void testValidateChannel_EmptyParams() {
    AlertChannel channel =
        new AlertChannel().setCustomerUUID(defaultCustomerUuid).setName(CHANNEL_NAME);

    try {
      alertChannelService.validate(channel);
      fail("YWValidateException is expected.");
    } catch (PlatformServiceException e) {
      assertThat(e.getMessage(), is("errorJson: {\"params\":[\"must not be null\"]}"));
    }
  }

  @Test
  public void testValidateChannel_HappyPath() {
    alertChannelService.validate(
        ModelFactory.createEmailChannel(defaultCustomerUuid, CHANNEL_NAME));
  }

  @Test
  public void testUpdate() {
    AlertChannel channel =
        ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME + " 1");

    AlertChannel updatedChannel = alertChannelService.get(defaultCustomerUuid, channel.getUuid());
    updatedChannel.setName(CHANNEL_NAME);

    AlertChannelEmailParams params = new AlertChannelEmailParams();
    params.setRecipients(Collections.singletonList("test@test.com"));
    params.setSmtpData(EmailFixtures.createSmtpData());
    updatedChannel.setParams(params);

    alertChannelService.save(updatedChannel);

    AlertChannel updatedFromDb = alertChannelService.get(defaultCustomerUuid, channel.getUuid());
    assertThat(updatedChannel, equalTo(updatedFromDb));
  }

  @Test
  public void testSave_DuplicateName_Fail() {
    AlertChannel channel =
        ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME + " 1");

    ModelFactory.createSlackChannel(defaultCustomerUuid, CHANNEL_NAME + " 2");

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
        equalTo("errorJson: {\"name\":[\"alert channel with such name already exists.\"]}"));
  }

  @Test
  public void testSave_LongName_Fail() {
    StringBuilder longName = new StringBuilder();
    while (longName.length() <= 63) {
      longName.append(CHANNEL_NAME);
    }

    AlertChannel channel =
        new AlertChannel()
            .setCustomerUUID(defaultCustomerUuid)
            .setName(longName.toString())
            .setParams(ModelFactory.createSlackChannelParams());

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertChannelService.save(channel);
            });
    assertThat(
        exception.getMessage(),
        equalTo("errorJson: {\"name\":[\"size must be between 1 and 63\"]}"));
  }

  @Test
  // @formatter:off
  @Parameters({
    "null, http://www.google.com, null, errorJson: "
        + "{\"params.username\":[\"must not be null\"]}",
    "channel, null, null, errorJson: " + "{\"params.webhookUrl\":[\"must not be null\"]}",
    "channel, incorrect url, null, errorJson: "
        + "{\"params.webhookUrl\":[\"must be a valid URL\"]}",
    "channel, http://www.google.com, null, null",
    "channel, http://www.google.com, incorrect url, errorJson: "
        + "{\"params.iconUrl\":[\"must be a valid URL\"]}",
    "channel, http://www.google.com, http://www.google.com, null",
  })
  // @formatter:on
  public void testSlackParamsValidate(
      @Nullable String username,
      @Nullable String webHookUrl,
      @Nullable String iconUrl,
      @Nullable String expectedError) {
    AlertChannelSlackParams params = new AlertChannelSlackParams();
    params.setUsername(username);
    params.setWebhookUrl(webHookUrl);
    params.setIconUrl(iconUrl);

    AlertChannel channel =
        new AlertChannel()
            .setCustomerUUID(defaultCustomerUuid)
            .setName(CHANNEL_NAME)
            .setParams(params);

    if (expectedError != null) {
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class,
              () -> {
                alertChannelService.validate(channel);
              });
      assertThat(exception.getMessage(), equalTo(expectedError));
    } else {
      alertChannelService.validate(channel);
    }
  }

  @Test
  @Parameters({
    "null, key, errorJson: {\"params.apiKey\":[\"must not be null\"]}",
    "key, null, errorJson: {\"params.routingKey\":[\"must not be null\"]}",
    "key1, key2, null",
  })
  // @formatter:on
  public void testPagerDutyParamsValidate(
      @Nullable String apiKey, @Nullable String routingKey, @Nullable String expectedError) {
    AlertChannelPagerDutyParams params = new AlertChannelPagerDutyParams();
    params.setApiKey(apiKey);
    params.setRoutingKey(routingKey);

    AlertChannel channel =
        new AlertChannel()
            .setCustomerUUID(defaultCustomerUuid)
            .setName(CHANNEL_NAME)
            .setParams(params);

    if (expectedError != null) {
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class,
              () -> {
                alertChannelService.validate(channel);
              });
      assertThat(exception.getMessage(), equalTo(expectedError));
    } else {
      alertChannelService.validate(channel);
    }
  }

  @Test
  @Parameters({
    "null, errorJson: {\"params.webhookUrl\":[\"must not be null\"]}",
    "string, errorJson: {\"params.webhookUrl\":[\"must be a valid URL\"]}",
    "http://www.google.com, null",
  })
  // @formatter:on
  public void testWebHookParamsValidate(
      @Nullable String webhookUrl, @Nullable String expectedError) {
    AlertChannelWebHookParams params = new AlertChannelWebHookParams();
    params.setWebhookUrl(webhookUrl);

    AlertChannel channel =
        new AlertChannel()
            .setCustomerUUID(defaultCustomerUuid)
            .setName(CHANNEL_NAME)
            .setParams(params);

    if (expectedError != null) {
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class,
              () -> {
                alertChannelService.validate(channel);
              });
      assertThat(exception.getMessage(), equalTo(expectedError));
    } else {
      alertChannelService.validate(channel);
    }
  }

  @Test
  // @formatter:off
  @Parameters({
    "null, false, true, errorJson: "
        + "{\"params\":[\"only one of defaultRecipients and recipients[] should be set.\"]}",
    "test@test, false, true, errorJson: "
        + "{\"params.recipients\":[\"invalid email address test@test\"]}",
    "test@test.com; test2@test2.com, false, true, null",
    "test1@test1.com; test2@test2.com; test@test, false, true, errorJson: "
        + "{\"params.recipients\":[\"invalid email address test@test\"]}",
    "test@test.com, true, true, errorJson: "
        + "{\"params\":[\"only one of defaultSmtpSettings and smtpData should be set.\"]}",
    "test@test.com, true, false, null",
  })
  // @formatter:on
  public void testEmailParamsValidate(
      @Nullable String destinations,
      boolean setSmtpData,
      boolean useDefaultSmtp,
      @Nullable String expectedError) {
    AlertChannelEmailParams params = new AlertChannelEmailParams();
    params.setRecipients(
        destinations != null
            ? new ArrayList<>(
                EmailHelper.splitEmails(destinations, EmailHelper.DEFAULT_EMAIL_SEPARATORS))
            : Collections.emptyList());
    params.setSmtpData(setSmtpData ? new SmtpData() : null);
    params.setDefaultSmtpSettings(useDefaultSmtp);

    AlertChannel channel =
        new AlertChannel()
            .setCustomerUUID(defaultCustomerUuid)
            .setName(CHANNEL_NAME)
            .setParams(params);

    if (expectedError != null) {
      PlatformServiceException exception =
          assertThrows(
              PlatformServiceException.class,
              () -> {
                alertChannelService.validate(channel);
              });
      assertThat(exception.getMessage(), equalTo(expectedError));
    } else {
      alertChannelService.validate(channel);
    }
  }
}
