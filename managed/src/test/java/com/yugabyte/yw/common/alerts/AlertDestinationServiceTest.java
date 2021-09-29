// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static com.yugabyte.yw.models.helpers.CommonUtils.nowWithoutMillis;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDestination;
import com.yugabyte.yw.models.Customer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertDestinationServiceTest extends FakeDBApplication {

  private static final String ALERT_DESTINATION_NAME = "Test AlertDestination";

  private Customer defaultCustomer;
  private UUID customerUUID;
  private AlertChannel channel;

  private AlertDestinationService alertDestinationService;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    customerUUID = defaultCustomer.getUuid();
    channel = ModelFactory.createEmailChannel(customerUUID, "Test AlertChannel");

    alertDestinationService = app.injector().instanceOf(AlertDestinationService.class);
  }

  @Test
  public void testGet() {
    AlertDestination destination =
        ModelFactory.createAlertDestination(
            customerUUID, ALERT_DESTINATION_NAME, Collections.singletonList(channel));

    AlertDestination fromDb = alertDestinationService.get(customerUUID, destination.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(destination));

    // Check get for random UUID - should return null.
    assertThat(alertDestinationService.get(customerUUID, UUID.randomUUID()), nullValue());
  }

  @Test
  public void testGetOrBadRequest() {
    AlertDestination destination =
        ModelFactory.createAlertDestination(
            customerUUID, ALERT_DESTINATION_NAME, Collections.singletonList(channel));

    AlertDestination fromDb =
        alertDestinationService.getOrBadRequest(customerUUID, destination.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(destination));

    // Should raise an exception for random UUID.
    final UUID uuid = UUID.randomUUID();
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertDestinationService.getOrBadRequest(customerUUID, uuid);
            });
    assertThat(exception.getMessage(), equalTo("Invalid Alert Destination UUID: " + uuid));
  }

  @Test
  public void testListByCustomer() {
    List<AlertDestination> destinations = new ArrayList<>();
    for (int i = 0; i < 10; i++) {
      destinations.add(
          ModelFactory.createAlertDestination(
              customerUUID, ALERT_DESTINATION_NAME + " " + i, Collections.singletonList(channel)));
    }

    List<AlertDestination> destinations2 = alertDestinationService.listByCustomer(customerUUID);
    assertThat(destinations2.size(), equalTo(destinations.size()));
    for (AlertDestination destination : destinations) {
      assertThat(destinations2.contains(destination), is(true));
    }
  }

  @Test
  public void testCreate_UnsavedChannel_Fail() {
    AlertChannel channel = new AlertChannel();
    channel.setUuid(UUID.randomUUID());
    AlertDestination destination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_DESTINATION_NAME)
            .setChannelsList(Collections.singletonList(channel));
    assertThrows(PlatformServiceException.class, () -> alertDestinationService.save(destination));
  }

  @Test
  public void testCreate_NoChannels_Fail() {
    final AlertDestination destination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_DESTINATION_NAME)
            .setChannelsList(Collections.emptyList());
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class, () -> alertDestinationService.save(destination));
    assertThat(
        exception.getMessage(),
        equalTo("errorJson: {\"channels\":[\"size must be between 1 and 2147483647\"]}"));
  }

  @Test
  public void testCreate_NonDefaultDestination_HappyPath() {
    AlertDestination destination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_DESTINATION_NAME)
            .setChannelsList(Collections.singletonList(channel));
    alertDestinationService.save(destination);
    assertThat(alertDestinationService.getDefaultDestination(customerUUID), nullValue());
  }

  @Test
  public void testCreate_DefaultDestination_HappyPath() {
    AlertDestination defaultDestination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_DESTINATION_NAME)
            .setChannelsList(Collections.singletonList(channel))
            .setDefaultDestination(true);
    alertDestinationService.save(defaultDestination);
    assertThat(
        alertDestinationService.getDefaultDestination(customerUUID), equalTo(defaultDestination));
  }

  @Test
  public void testSave_UpdateToNonDefault_Fail() {
    AlertDestination defaultDestination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_DESTINATION_NAME)
            .setChannelsList(Collections.singletonList(channel))
            .setDefaultDestination(true);
    defaultDestination = alertDestinationService.save(defaultDestination);

    final AlertDestination updatedDestination = defaultDestination.setDefaultDestination(false);
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class, () -> alertDestinationService.save(updatedDestination));
    assertThat(
        exception.getMessage(),
        equalTo(
            "errorJson: {\"defaultDestination\":[\"can't set the alert destination "
                + "as non-default - make another destination as default at first.\"]}"));
  }

  @Test
  public void testSave_SwitchDefaultDestination_HappyPath() {
    AlertDestination oldDefaultDestination =
        alertDestinationService.createDefaultDestination(customerUUID);
    AlertDestination newDefaultDestination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_DESTINATION_NAME)
            .setChannelsList(Collections.singletonList(channel))
            .setDefaultDestination(true);
    newDefaultDestination = alertDestinationService.save(newDefaultDestination);

    assertThat(newDefaultDestination.isDefaultDestination(), is(true));
    assertThat(
        newDefaultDestination,
        equalTo(alertDestinationService.getDefaultDestination(customerUUID)));
    assertThat(
        alertDestinationService
            .get(customerUUID, oldDefaultDestination.getUuid())
            .isDefaultDestination(),
        is(false));
  }

  @Test
  public void testDelete_HappyPath() {
    AlertDestination destination =
        ModelFactory.createAlertDestination(
            customerUUID, ALERT_DESTINATION_NAME, Collections.singletonList(channel));

    assertThat(alertDestinationService.get(customerUUID, destination.getUuid()), notNullValue());
    alertDestinationService.delete(customerUUID, destination.getUuid());
    assertThat(alertDestinationService.get(customerUUID, destination.getUuid()), nullValue());
  }

  @Test
  public void testDelete_DefaultDestination_Fail() {
    AlertDestination defaultDestination =
        alertDestinationService.createDefaultDestination(customerUUID);
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertDestinationService.delete(customerUUID, defaultDestination.getUuid());
            });
    assertThat(
        exception.getMessage(),
        equalTo(
            "Unable to delete default alert destination '"
                + defaultDestination.getName()
                + "', make another destination default at first."));
  }

  @Test
  public void testDelete_UsedByAlertConfiguration_Fail() {
    AlertDestination destination =
        ModelFactory.createAlertDestination(
            customerUUID, ALERT_DESTINATION_NAME, Collections.singletonList(channel));
    AlertConfiguration configuration =
        alertConfigurationService
            .createConfigurationTemplate(defaultCustomer, AlertTemplate.MEMORY_CONSUMPTION)
            .getDefaultConfiguration();
    configuration.setCreateTime(nowWithoutMillis());
    configuration.setDestinationUUID(destination.getUuid());
    configuration.save();

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertDestinationService.delete(customerUUID, destination.getUuid());
            });
    assertThat(
        exception.getMessage(),
        equalTo(
            "Unable to delete alert destination '"
                + destination.getName()
                + "'. 1 alert configurations are linked to it. Examples: ["
                + configuration.getName()
                + "]"));
  }

  @Test
  public void testSave_DuplicateName_Fail() {
    AlertDestination destination =
        ModelFactory.createAlertDestination(
            customerUUID, ALERT_DESTINATION_NAME + " 1", Collections.singletonList(channel));

    ModelFactory.createAlertDestination(
        customerUUID, ALERT_DESTINATION_NAME + " 2", Collections.singletonList(channel));

    AlertDestination updatedDestination =
        alertDestinationService.get(customerUUID, destination.getUuid());
    // Setting duplicate name.
    updatedDestination.setName(ALERT_DESTINATION_NAME + " 2");

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertDestinationService.save(updatedDestination);
            });
    assertThat(
        exception.getMessage(),
        equalTo("errorJson: {\"name\":[\"alert destination with such name already exists.\"]}"));
  }

  @Test
  public void testSave_LongName_Fail() {
    StringBuilder longName = new StringBuilder();
    while (longName.length() <= 63) {
      longName.append(ALERT_DESTINATION_NAME);
    }

    AlertDestination destination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName(longName.toString())
            .setChannelsList(Collections.singletonList(channel));

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertDestinationService.save(destination);
            });
    assertThat(
        exception.getMessage(),
        equalTo("errorJson: {\"name\":[\"size must be between 1 and 63\"]}"));
  }
}
