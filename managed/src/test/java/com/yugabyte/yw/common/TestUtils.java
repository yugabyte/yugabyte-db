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
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
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
import org.apache.commons.io.IOUtils;
import play.libs.Json;

public class TestUtils {
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
}
