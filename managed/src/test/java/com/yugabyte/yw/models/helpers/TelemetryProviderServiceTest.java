// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.helpers;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.*;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.*;
import static org.mockito.Mockito.doNothing;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.WSClientRefresher;
import com.yugabyte.yw.models.TelemetryProvider;
import com.yugabyte.yw.models.helpers.telemetry.DataDogConfig;
import com.yugabyte.yw.models.helpers.telemetry.ProviderType;
import com.yugabyte.yw.models.helpers.telemetry.TelemetryProviderConfig;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockitoAnnotations;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class TelemetryProviderServiceTest extends FakeDBApplication {

  private UUID defaultCustomerUuid;
  private TelemetryProviderService telemetryProviderService;

  @Mock WSClientRefresher wsClientRefresher;
  @Mock BeanValidator beanValidator;
  @Mock BeanValidator.ErrorMessageBuilder errorMessageBuilder;

  @Before
  public void setUp() {
    // Initialize mocks manually since we're using JUnitParamsRunner
    MockitoAnnotations.openMocks(this);

    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();

    // Mock the BeanValidator and its ErrorMessageBuilder
    doNothing().when(beanValidator).validate(any());
    when(beanValidator.error()).thenReturn(errorMessageBuilder);
    when(errorMessageBuilder.forField(anyString(), anyString())).thenReturn(errorMessageBuilder);

    // Mock throwError to actually throw the expected exception
    doAnswer(
            invocation -> {
              Map<String, List<String>> errors = new HashMap<>();
              errors.put("name", Arrays.asList("provider with such name already exists."));
              JsonNode errJson = Json.toJson(errors);
              throw new PlatformServiceException(BAD_REQUEST, errJson);
            })
        .when(errorMessageBuilder)
        .throwError();

    // Mock WSClientRefresher to avoid the getApiHelper issue
    when(wsClientRefresher.getClient(anyString())).thenReturn(null);

    // Create a real service instance with mocked dependencies
    telemetryProviderService = new TelemetryProviderService(beanValidator, null, wsClientRefresher);

    // Create a spy to override only the problematic getApiHelper method
    telemetryProviderService = spy(telemetryProviderService);
    doReturn(mockApiHelper).when(telemetryProviderService).getApiHelper();

    // Mock the API helper responses
    when(mockApiHelper.getRequest(anyString(), anyMap(), anyMap())).thenReturn(Json.parse("{}"));
  }

  @Parameters({"DataDog", "Splunk", "AWSCloudWatch", "GCPCloudMonitoring"})
  @Test
  public void testSerialization(String jsonFileName) throws IOException {

    String initial = TestUtils.readResource("telemetry/" + jsonFileName + ".json");

    JsonNode initialJson = Json.parse(initial);

    TelemetryProvider settings = Json.fromJson(initialJson, TelemetryProvider.class);

    JsonNode resultJson = Json.toJson(settings);

    assertThat(resultJson, equalTo(initialJson));
  }

  @Test
  public void testCreateAndGet() {
    TelemetryProvider provider = createTestProvider("test");
    TelemetryProvider updated = telemetryProviderService.save(provider);

    assertThat(updated, equalTo(provider));

    TelemetryProvider fromDb = telemetryProviderService.get(provider.getUuid());
    assertThat(fromDb, equalTo(provider));
  }

  @Test
  public void testGetOrBadRequest() {
    // Should raise an exception for random UUID.
    final UUID uuid = UUID.randomUUID();
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              telemetryProviderService.getOrBadRequest(uuid);
            });
    assertThat(exception.getMessage(), equalTo("Invalid Telemetry Provider UUID: " + uuid));
  }

  @Test
  public void testListByCustomerUuid() {
    TelemetryProvider provider = createTestProvider("test");
    telemetryProviderService.save(provider);

    TelemetryProvider provider2 = createTestProvider("test2");
    telemetryProviderService.save(provider2);

    UUID newCustomerUUID = ModelFactory.testCustomer().getUuid();
    TelemetryProvider otherCustomerProvider = createTestProvider(newCustomerUUID, "test2");

    List<TelemetryProvider> providers = telemetryProviderService.list(defaultCustomerUuid);
    assertThat(providers, containsInAnyOrder(provider, provider2));
  }

  @Test
  public void testValidateDuplicateName() {
    TelemetryProvider provider = createTestProvider("test");
    telemetryProviderService.save(provider);

    TelemetryProvider duplicate = createTestProvider("test");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              telemetryProviderService.save(duplicate);
            });
    assertThat(
        exception.getMessage(),
        equalTo("errorJson: {\"name\":[\"provider with such name already exists.\"]}"));
  }

  @Test
  public void testDelete() {
    TelemetryProvider provider = createTestProvider("test");
    telemetryProviderService.save(provider);

    telemetryProviderService.delete(provider.getUuid());

    TelemetryProvider fromDb = telemetryProviderService.get(provider.getUuid());
    assertThat(fromDb, nullValue());
  }

  private TelemetryProvider createTestProvider(String name) {
    return createTestProvider(defaultCustomerUuid, name);
  }

  public static TelemetryProvider createTestProvider(UUID customerUUID, String name) {
    TelemetryProvider provider = new TelemetryProvider();
    provider.setName(name);
    provider.setCustomerUUID(customerUUID);
    Map<String, String> tags = new HashMap<>();
    tags.put("user1", name);
    tags.put("address", "CA");
    provider.setTags(tags);

    TelemetryProviderConfig config = new TelemetryProviderConfig();
    config.setType(ProviderType.DATA_DOG);
    if (config.getType() == ProviderType.DATA_DOG) {
      DataDogConfig dataDogConfig = new DataDogConfig();
      dataDogConfig.setApiKey("data-dog-api-key");
      dataDogConfig.setSite("us3.datadoghq.com");
      provider.setConfig(dataDogConfig);
    }
    return provider;
  }
}
