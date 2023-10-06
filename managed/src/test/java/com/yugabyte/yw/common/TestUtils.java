/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
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
}
