/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common;

import static io.prometheus.metrics.model.registry.PrometheusRegistry.defaultRegistry;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.closeTo;
import static org.junit.Assert.fail;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import io.prometheus.metrics.model.snapshots.CounterSnapshot.CounterDataPointSnapshot;
import io.prometheus.metrics.model.snapshots.DataPointSnapshot;
import io.prometheus.metrics.model.snapshots.GaugeSnapshot.GaugeDataPointSnapshot;
import io.prometheus.metrics.model.snapshots.Labels;
import io.prometheus.metrics.model.snapshots.MetricSnapshot;
import io.prometheus.metrics.model.snapshots.MetricSnapshots;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.UUID;
import java.util.function.Consumer;
import org.apache.commons.io.IOUtils;
import play.libs.Json;

public class TestUtils {
  private static final boolean MULTIPROVIDER_ENABLED_IN_TESTS = false;

  public static String readResource(String path) {
    try {
      return IOUtils.toString(
          TestUtils.class.getClassLoader().getResourceAsStream(path), StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new RuntimeException("Failed to read resource " + path, e);
    }
  }

  public static JsonNode readResourceAsJson(String path) {
    String resourceStr = readResource(path);
    return Json.parse(resourceStr);
  }

  public static UUID replaceFirstChar(UUID uuid, char firstChar) {
    char[] chars = uuid.toString().toCharArray();
    chars[0] = firstChar;
    return UUID.fromString(new String(chars));
  }

  public static <T> T deserialize(String json, Class<T> type) {
    try {
      return new ObjectMapper().readValue(json, type);
    } catch (Exception e) {
      throw new RuntimeException("Error deserializing object: ", e);
    }
  }

  public static void setFakeHttpContext(Users user) {
    setFakeHttpContext(user, "sg@yftt.com");
  }

  public static void setFakeHttpContext(Users user, String email) {
    if (user != null) {
      user.setEmail(email);
    }
    RequestContext.put(TokenAuthenticator.USER, new UserWithFeatures().setUser(user));
  }

  public static String generateLongString(int length) {
    char[] chars = new char[length];
    Arrays.fill(chars, 'A');
    return new String(chars);
  }

  public static UniverseDefinitionTaskParams.UserIntentOverrides composeAZOverrides(
      UUID azUUID, String instanceType, Integer cgroupSize) {
    UniverseDefinitionTaskParams.UserIntentOverrides result =
        new UniverseDefinitionTaskParams.UserIntentOverrides();
    UniverseDefinitionTaskParams.AZOverrides azOverrides =
        new UniverseDefinitionTaskParams.AZOverrides();
    azOverrides.setCgroupSize(cgroupSize);
    azOverrides.setInstanceType(instanceType);
    result.setAzOverrides(ImmutableMap.of(azUUID, azOverrides));
    return result;
  }

  public static void validateMetric(String name, Double value, String... labels) {
    Double actualValue = getMetricValue(name, labels);
    if (actualValue == null && value == null) {
      return;
    }
    if (actualValue == null) {
      fail("Metric value is not found");
    }
    assertThat(actualValue, closeTo(value, 0.1));
  }

  public static Double getMetricValue(String name, String... labels) {
    MetricSnapshots snapshots = defaultRegistry.scrape(n -> n.equals(name));
    for (MetricSnapshot snapshot : snapshots) {
      for (DataPointSnapshot dataPoint : snapshot.getDataPoints()) {
        if (dataPoint.getLabels().equals(Labels.of(labels))) {
          if (dataPoint instanceof GaugeDataPointSnapshot) {
            return ((GaugeDataPointSnapshot) dataPoint).getValue();
          }
          if (dataPoint instanceof CounterDataPointSnapshot) {
            return ((CounterDataPointSnapshot) dataPoint).getValue();
          }
        }
      }
    }
    return null;
  }

  // Should already have a single provider.
  public static ProviderInitializer updateInstanceType(
      UniverseDefinitionTaskParams.UserIntent userIntent, String newInstanceType) {
    return updateInstanceType(userIntent, UniverseTaskBase.ServerType.TSERVER, newInstanceType);
  }

  // Should already have a single provider.
  public static ProviderInitializer updateInstanceType(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      UniverseTaskBase.ServerType serverType,
      String newInstanceType) {
    ProviderInitializer providerInitializer = existingProviderInitializer(userIntent);
    if (serverType == UniverseTaskBase.ServerType.MASTER) {
      return providerInitializer.setMasterInstanceType(newInstanceType);
    } else {
      return providerInitializer.setInstanceType(newInstanceType);
    }
  }

  // Should already have a single provider.
  public static void updateDeviceInfo(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      UniverseTaskBase.ServerType serverType,
      Consumer<DeviceInfo> mutator) {
    UUID providerUUID = userIntent.maybeGetSingleProviderUUID().get();
    mutator.accept(userIntent.getBaseDeviceInfo(providerUUID, serverType));
  }

  public static ProviderInitializer copyMasterDeviceInfoFromDeviceInfo(
      UniverseDefinitionTaskParams.UserIntent userIntent) {
    UUID providerUUID = userIntent.maybeGetSingleProviderUUID().get();
    DeviceInfo deviceInfo = userIntent.getBaseDeviceInfo(providerUUID);
    return existingProviderInitializer(userIntent).setMasterDeviceInfo(deviceInfo.clone());
  }

  // Should already have a single provider.
  public static ProviderInitializer existingProviderInitializer(
      UniverseDefinitionTaskParams.UserIntent userIntent) {
    return Util.providerInitializerForExistingIntent(
        userIntent, userIntent.maybeGetSingleProviderUUID().get());
  }

  public static ProviderInitializer specificationProviderInitializer(
      UniverseDefinitionTaskParams.UserIntent userIntent, UUID providerUUID) {
    return new SpecificationProviderInitializer(userIntent, providerUUID);
  }

  public static ProviderInitializer getProviderInitializerForTests(
      UniverseDefinitionTaskParams.UserIntent userIntent, UUID providerUUID) {
    return MULTIPROVIDER_ENABLED_IN_TESTS
        ? new SpecificationProviderInitializer(userIntent, providerUUID)
        : new IntentProviderInitializer(userIntent, providerUUID);
  }

  public static ProviderInitializer intentProviderInitializer(
      UniverseDefinitionTaskParams.UserIntent userIntent, UUID providerUUID) {
    return new IntentProviderInitializer(userIntent, providerUUID);
  }

  public static ProviderInitializer intentProviderInitializer(
      UniverseDefinitionTaskParams.UserIntent userIntent, Provider provider) {
    ProviderInitializer result = new IntentProviderInitializer(userIntent, provider.getUuid());
    result.setProviderType(provider.getCloudCode());
    return result;
  }

  public static ProviderInitializer initUserIntent(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Provider provider,
      String instanceType,
      DeviceInfo deviceInfo,
      String accessKeyCode) {
    return initUserIntent(
        userIntent,
        provider.getUuid(),
        provider.getCloudCode(),
        instanceType,
        deviceInfo,
        accessKeyCode);
  }

  public static ProviderInitializer initUserIntent(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      UUID providerUUD,
      Common.CloudType cloudType,
      String instanceType,
      DeviceInfo deviceInfo,
      String accessKeyCode) {
    ProviderInitializer providerInitializer =
        getProviderInitializerForTests(userIntent, providerUUD);
    providerInitializer.setProviderType(cloudType);
    providerInitializer.setInstanceType(instanceType);
    providerInitializer.setDeviceInfo(deviceInfo);
    providerInitializer.setAccessCode(accessKeyCode);
    return providerInitializer;
  }

  public static ProviderInitializer copyProviderFields(
      UniverseDefinitionTaskParams.UserIntent curIntent,
      UniverseDefinitionTaskParams.UserIntent newIntent,
      RuntimeConfGetter confGetter) {
    UUID providerUUID = curIntent.maybeGetSingleProviderUUID().get();
    ProviderInitializer providerInitializer =
        getProviderInitializerForTests(newIntent, providerUUID);

    providerInitializer.setAccessCode(curIntent.getAccessKeyCodeForProvider(providerUUID));
    providerInitializer.setProviderType(curIntent.getAllCloudTypes().iterator().next());
    providerInitializer.setInstanceType(curIntent.getBaseInstanceType(providerUUID));

    DeviceInfo deviceInfo = curIntent.getBaseDeviceInfo(providerUUID).clone();
    deviceInfo.numVolumes = 2;

    providerInitializer.setDeviceInfo(deviceInfo);
    providerInitializer.setAccessCode(curIntent.getAccessKeyCodeForProvider(providerUUID));
    return providerInitializer;
  }
}
