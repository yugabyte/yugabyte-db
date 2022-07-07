// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.password;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.jayway.jsonpath.Configuration;
import com.jayway.jsonpath.DocumentContext;
import com.jayway.jsonpath.JsonPath;
import com.jayway.jsonpath.spi.json.JacksonJsonNodeJsonProvider;
import com.jayway.jsonpath.spi.mapper.JacksonMappingProvider;
import java.util.List;
import java.util.stream.Collectors;
import javax.inject.Singleton;

@Singleton
public class RedactingService {
  public static final String SECRET_REPLACEMENT = "REDACTED";
  private static final List<String> SECRET_PATHS =
      ImmutableList.of(
          "$..ysqlPassword",
          "$..ycqlPassword",
          "$..ycqlNewPassword",
          "$..ysqlNewPassword",
          "$..ycqlCurrentPassword",
          "$..ysqlCurrentPassword");
  public static final List<JsonPath> SECRET_JSON_PATHS =
      SECRET_PATHS.stream().map(JsonPath::compile).collect(Collectors.toList());

  private static final Configuration JSONPATH_CONFIG =
      Configuration.builder()
          .jsonProvider(new JacksonJsonNodeJsonProvider())
          .mappingProvider(new JacksonMappingProvider())
          .build();

  public static JsonNode filterSecretFields(JsonNode input) {
    if (input == null) {
      return null;
    }
    DocumentContext context = JsonPath.parse(input.deepCopy(), JSONPATH_CONFIG);
    SECRET_JSON_PATHS.forEach(path -> context.set(path, SECRET_REPLACEMENT));
    return context.json();
  }
}
