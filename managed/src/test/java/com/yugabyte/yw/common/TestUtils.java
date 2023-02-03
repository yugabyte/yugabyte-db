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
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.Map;
import java.util.UUID;
import org.apache.commons.io.IOUtils;

import play.libs.Json;
import play.mvc.Http;
import play.mvc.Http.Context;

import static org.mockito.Mockito.mock;
import static play.test.Helpers.contextComponents;

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
    Map<String, String> flashData = Collections.emptyMap();
    if (user != null) {
      user.email = email;
    }
    Map<String, Object> argData = ImmutableMap.of("user", new UserWithFeatures().setUser(user));
    Http.Request request = mock(Http.Request.class);
    Long id = 2L;
    play.api.mvc.RequestHeader header = mock(play.api.mvc.RequestHeader.class);
    Context currentContext =
        new Context(id, header, request, flashData, flashData, argData, contextComponents());
    Context.current.set(currentContext);
  }
}
